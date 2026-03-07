<?php

namespace App\Http\Controllers;

use App\Models\Device;
use App\Models\Alert;
use App\Models\Collection;
use App\Events\AlertCreated;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Log;

class EspController extends Controller
{
    /**
     * Receive raw sensor data from ESP32 device.
     * POST /api/bin-data
     * 
     * This is the PRIMARY endpoint for ESP32 hardware.
     * Accepts raw sensor values and performs all derivation server-side.
     */
    public function receiveBinData(Request $request)
    {
        // 1. Validate the raw payload from ESP32
        $validated = $request->validate([
            'device_id'         => 'required|string|max:50',
            'battery_voltage'   => 'nullable|required_without:battery_adc|numeric|min:0|max:20',
            'battery_adc'       => 'nullable|required_without:battery_voltage|numeric|min:0|max:4095',
            'bin_1'             => 'required|array',
            'bin_1.distance_cm' => 'required|numeric',
            'bin_1.hx711_raw'   => 'required|numeric',
            'bin_1.mq_raw'      => 'required|numeric|min:0',
            'bin_2'             => 'required|array',
            'bin_2.distance_cm' => 'required|numeric',
            'bin_2.hx711_raw'   => 'required|numeric',
            'bin_2.mq_raw'      => 'required|numeric|min:0',
        ]);

        $parentDeviceId = $validated['device_id'];
        
        // 2. Derive battery percentage (shared between both bins)
        $batteryVoltage = array_key_exists('battery_voltage', $validated) && $validated['battery_voltage'] !== null
            ? (float) $validated['battery_voltage']
            : $this->deriveBatteryVoltageFromAdc((int) $validated['battery_adc']);
        $batteryPercent = $this->deriveBatteryPercent($batteryVoltage);

        Log::info("ESP32 Data Received", [
            'device_id' => $parentDeviceId,
            'battery_voltage' => round($batteryVoltage, 3),
            'battery_adc' => $validated['battery_adc'] ?? null,
            'battery_percent' => $batteryPercent,
        ]);

        // 3. Process each bin
        $results = [];
        foreach (['bin_1' => 1, 'bin_2' => 2] as $binKey => $binNumber) {
            $binData = $validated[$binKey];
            
            $result = $this->processBinData(
                $parentDeviceId,
                $binNumber,
                $binData,
                $batteryPercent
            );
            
            $results["bin_{$binNumber}"] = $result;
        }

        return response()->json([
            'status' => 'ok',
            'message' => 'Data received and processed',
            'device_id' => $parentDeviceId,
            'battery_voltage' => round($batteryVoltage, 3),
            'battery_percent' => round($batteryPercent, 1),
            'bins' => $results,
        ]);
    }

    /**
     * Process data for a single bin
     */
    private function processBinData(string $parentDeviceId, int $binNumber, array $binData, float $batteryPercent): array
    {
        // Create unique identifier for this bin
        $binUid = "{$parentDeviceId}_bin{$binNumber}";

        // Auto-register device if it doesn't exist
        $device = Device::firstOrCreate(
            ['uid' => $binUid],
            [
                'name' => "Smart Bin {$binNumber}",
                'location' => 'Unassigned Location',
                'parent_device_id' => $parentDeviceId,
                'bin_number' => $binNumber,
                'is_active' => true,
            ]
        );

        $metadataUpdates = $this->defaultLocationUpdates($device, $binNumber);

        // Update device status
        $device->update([
            ...$metadataUpdates,
            'last_seen_at' => now(),
            'battery_percent' => $batteryPercent,
        ]);

        // Derive values from raw sensor data
        $fillPercent = $this->deriveFillPercent($binData['distance_cm']);
        $weightKg = $this->deriveWeight($binData['hx711_raw'], $binNumber);
        $gasLevel = $this->deriveGasLevel($binData['mq_raw']);

        // Collection detection (significant fill drop)
        $collectionDetected = $this->detectCollection($device, $fillPercent);

        // Alert checks
        $this->checkFillAlerts($device, $fillPercent);
        $this->checkGasAlerts($device, $gasLevel);

        // Store all readings with raw values
        $device->readings()->createMany([
            [
                'type' => 'fill',
                'value' => $fillPercent,
                'raw_value' => $binData['distance_cm'],
                'unit' => '%'
            ],
            [
                'type' => 'weight',
                'value' => $weightKg,
                'raw_value' => $binData['hx711_raw'],
                'unit' => 'kg'
            ],
            [
                'type' => 'gas',
                'value' => $gasLevel,
                'raw_value' => $binData['mq_raw'],
                'unit' => 'level'
            ],
            [
                'type' => 'battery',
                'value' => $batteryPercent,
                'raw_value' => null, // Shared across bins
                'unit' => '%'
            ],
        ]);

        return [
            'device_id' => $device->id,
            'uid' => $binUid,
            'fill_percent' => round($fillPercent, 3),
            'weight_kg' => round($weightKg, 3),
            'gas_level' => $gasLevel,
            'collection_detected' => $collectionDetected,
        ];
    }

    private function defaultLocationUpdates(Device $device, int $binNumber): array
    {
        $defaults = Device::defaultMetadataForBin($binNumber);
        $updates = [];

        if (!$device->hasAssignedLocation() && $defaults['location']) {
            $updates['location'] = $defaults['location'];
        }

        if (!$device->hasAssignedCoordinates() && $defaults['latitude'] && $defaults['longitude']) {
            $updates['latitude'] = $defaults['latitude'];
            $updates['longitude'] = $defaults['longitude'];
        }

        return $updates;
    }

    /**
     * Derive fill percentage from ultrasonic distance
     * 
     * Formula: fill% = (empty_distance - actual_distance - offset) / (empty_distance - full_distance) * 100
     * 
     * Calibration values from Raw Values = Calibration.docx:
     * - Empty distance: 59 cm
     * - Full distance: 4 cm
     * - Offset: 3.5 cm (correction factor)
     */
    private function deriveFillPercent(float $distanceCm): float
    {
        // Handle sensor error (returns -1)
        if ($distanceCm < 0) {
            Log::warning("Ultrasonic sensor error: negative distance", ['distance' => $distanceCm]);
            return 0;
        }

        $emptyDistance = config('sensors.empty_distance_cm', 59.0);
        $fullDistance = config('sensors.full_distance_cm', 4.0);
        $offset = config('sensors.offset_cm', 3.5);

        // Apply offset correction
        $correctedDistance = $distanceCm + $offset;

        // Avoid division by zero
        if ($emptyDistance <= $fullDistance) {
            Log::error("Invalid ultrasonic calibration: empty_distance must be > full_distance");
            return 0;
        }

        $percent = (($emptyDistance - $correctedDistance) / ($emptyDistance - $fullDistance)) * 100;
        
        // Clamp between 0 and 100
        return max(0, min(100, $percent));
    }

    /**
     * Derive weight in kg from HX711 raw value
     * 
     * Formula: weight_kg = (hx711_raw - raw_empty) / scale
     * 
     * Calibration values from Raw Values = Calibration.docx:
     * Bin 1: raw_empty = 451,977, scale = 119,800
     * Bin 2: raw_empty = -491,000, scale = 117,786
     * 
     * @param float $hx711Raw Raw HX711 reading
     * @param int $binNumber 1 or 2 to select correct calibration
     */
    private function deriveWeight(float $hx711Raw, int $binNumber): float
    {
        $maxWeight = config('sensors.max_weight_kg', 20.0);
        
        // Select calibration based on bin number
        if ($binNumber === 1) {
            $rawEmpty = config('sensors.raw_empty_bin1', 451977);
            $scale = config('sensors.scale_bin1', 119800.0);
        } else {
            $rawEmpty = config('sensors.raw_empty_bin2', -491000);
            $scale = config('sensors.scale_bin2', 117786.0);
        }

        // Validate scale factor
        if ($scale == 0) {
            Log::error("Invalid weight calibration: scale factor is zero", ['bin' => $binNumber]);
            return 0;
        }

        // Calculate weight using calibration formula
        $weight = ($hx711Raw - $rawEmpty) / $scale;
        
        // Log calculation for debugging
        Log::debug("Weight calculation", [
            'bin' => $binNumber,
            'raw' => $hx711Raw,
            'raw_empty' => $rawEmpty,
            'scale' => $scale,
            'calculated_kg' => $weight
        ]);
        
        // Clamp between 0 and max weight
        return max(0, min($maxWeight, $weight));
    }

    /**
     * Derive gas level from MQ sensor raw value
     * 
     * Calibration values from Raw Values = Calibration.docx:
     * - Normal air: 100-300
     * - Alcohol spray (flammable): 600-900+
     * - Digital trigger: Active LOW (pin 25)
     * 
     * Returns: 0 = Normal, 1 = Elevated, 2 = Dangerous (Flammable)
     */
    private function deriveGasLevel(int $mqRaw): int
    {
        $normalMax = config('sensors.mq_normal_max', 300);
        $elevatedMin = config('sensors.mq_elevated_min', 300);
        $dangerousMin = config('sensors.mq_dangerous_min', 600);

        // Dangerous: Above 600 (calibrated flammable threshold)
        if ($mqRaw >= $dangerousMin) {
            return 2; // Dangerous
        }
        
        // Elevated: Between 300-600
        if ($mqRaw >= $elevatedMin) {
            return 1; // Elevated
        }
        
        // Normal: Below 300
        return 0; // Normal
    }

    /**
     * Convert a legacy ADC reading to battery voltage.
     * 
     * Formula: voltage = (adc / 4095) * 3.3 * voltage_divider
     */
    private function deriveBatteryVoltageFromAdc(int $adcRaw): float
    {
        $adcMax = config('sensors.adc_max_value', 4095);
        $reference = config('sensors.adc_reference', 3.3);
        $divider = config('sensors.voltage_divider', 2.0);

        if ($adcMax == 0) {
            return 0.0;
        }

        return ($adcRaw / $adcMax) * $reference * $divider;
    }

    /**
     * Derive battery percentage from reported battery voltage.
     * 
     * Formula: percent = (voltage - min) / (max - min) * 100
     */
    private function deriveBatteryPercent(float $batteryVoltage): float
    {
        $maxVoltage = config('sensors.battery_max_voltage', 12.6);
        $minVoltage = config('sensors.battery_min_voltage', 9.0);

        if ($maxVoltage == $minVoltage) {
            return 0.0;
        }

        $percent = (($batteryVoltage - $minVoltage) / ($maxVoltage - $minVoltage)) * 100;

        return max(0, min(100, $percent));
    }

    /**
     * Detect if a collection event occurred
     * 
     * A collection is detected when fill level drops significantly
     * (e.g., from >80% to <20%)
     */
    private function detectCollection(Device $device, float $newFillValue): bool
    {
        $highThreshold = config('sensors.collection_high_threshold', 80);
        $lowThreshold = config('sensors.collection_low_threshold', 20);

        // Get last fill reading
        $lastReading = $device->readings()
            ->where('type', 'fill')
            ->latest()
            ->first();

        if (!$lastReading) {
            return false;
        }

        $previousFill = $lastReading->value;

        // Check if significant drop occurred
        if ($previousFill >= $highThreshold && $newFillValue <= $lowThreshold) {
            // Create collection record
            Collection::create([
                'device_id' => $device->id,
                'collected_at' => now(),
                'amount_collected' => $previousFill, // Approximate amount
                'previous_fill' => $previousFill,
            ]);

            // Resolve any active "full" alerts
            $device->alerts()
                ->where('type', 'trash_full')
                ->where('status', 'active')
                ->update(['status' => 'resolved']);

            Log::info("Collection detected", [
                'device_id' => $device->id,
                'previous_fill' => $previousFill,
                'new_fill' => $newFillValue,
            ]);

            return true;
        }

        return false;
    }

    /**
     * Check and create fill level alerts
     */
    private function checkFillAlerts(Device $device, float $fillValue): void
    {
        if ($fillValue >= 95) {
            $alert = $device->alerts()->firstOrCreate(
                ['type' => 'trash_full', 'status' => 'active'],
                ['message' => "Bin is critical ({$fillValue}% full)"]
            );
            if ($alert->wasRecentlyCreated) {
                event(new AlertCreated($alert));
            }
        } elseif ($fillValue >= 80) {
            $alert = $device->alerts()->firstOrCreate(
                ['type' => 'trash_warning', 'status' => 'active'],
                ['message' => "Bin is getting full ({$fillValue}% full)"]
            );
            if ($alert->wasRecentlyCreated) {
                event(new AlertCreated($alert));
            }
        } elseif ($fillValue < 70) {
            $device->alerts()
                ->whereIn('type', ['trash_full', 'trash_warning'])
                ->where('status', 'active')
                ->update(['status' => 'resolved']);
        }
    }

    /**
     * Check and create gas level alerts
     */
    private function checkGasAlerts(Device $device, int $gasLevel): void
    {
        if ($gasLevel >= 2) {
            $alert = $device->alerts()->firstOrCreate(
                ['type' => 'gas_leak', 'status' => 'active'],
                ['message' => 'Dangerous gas level detected! Possible flammable gas.']
            );
            if ($alert->wasRecentlyCreated) {
                event(new AlertCreated($alert));
            }
        } elseif ($gasLevel >= 1) {
            $alert = $device->alerts()->firstOrCreate(
                ['type' => 'gas_elevated', 'status' => 'active'],
                ['message' => 'Elevated gas level detected. Monitor closely.']
            );
            if ($alert->wasRecentlyCreated) {
                event(new AlertCreated($alert));
            }
        } else {
            $device->alerts()
                ->whereIn('type', ['gas_leak', 'gas_elevated'])
                ->where('status', 'active')
                ->update(['status' => 'resolved']);
        }
    }

    /**
     * Legacy endpoint for backwards compatibility
     * POST /api/esp/readings
     * 
     * @deprecated Use receiveBinData() instead
     */
    public function storeReadings(Request $request)
    {
        // Validate legacy format
        $request->validate([
            'uid' => 'required|string|exists:devices,uid',
            'readings' => 'required|array',
            'readings.*.type' => 'required|string',
            'readings.*.value' => 'required|numeric'
        ]);

        $device = Device::where('uid', $request->uid)->firstOrFail();
        $device->update(['last_seen_at' => now()]);

        foreach ($request->readings as $reading) {
            if ($reading['type'] === 'fill') {
                $this->detectCollection($device, $reading['value']);
                $this->checkFillAlerts($device, $reading['value']);
            }
            
            if ($reading['type'] === 'gas') {
                $this->checkGasAlerts($device, (int) $reading['value']);
            }

            $device->readings()->create([
                'type' => $reading['type'],
                'value' => $reading['value'],
                'unit' => $this->getUnit($reading['type'])
            ]);
        }

        return response()->json(['status' => 'saved']);
    }

    /**
     * Get unit for reading type (legacy)
     */
    private function getUnit(string $type): string
    {
        return match ($type) {
            'fill', 'battery' => '%',
            'gas' => 'level',
            'weight' => 'kg',
            default => ''
        };
    }

    /**
     * Health check endpoint for ESP32
     * GET /api/esp/health
     */
    public function health()
    {
        return response()->json([
            'status' => 'ok',
            'timestamp' => now()->toIso8601String(),
            'server' => 'Laravel Trash Bin API',
        ]);
    }
}

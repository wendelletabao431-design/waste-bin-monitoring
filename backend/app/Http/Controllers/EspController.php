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
        // 1. Validate the raw payload from ESP32.
        //    bin_1 and bin_2 are OPTIONAL so battery-only payloads
        //    (sendBatteryOnly from firmware) are accepted without 422.
        $validated = $request->validate([
            'device_id'         => 'required|string|max:50',
            'battery_voltage'   => 'nullable|numeric|min:0|max:20',
            'battery_adc'       => 'nullable|numeric|min:0|max:4095',
            'bin_1'             => 'nullable|array',
            'bin_1.distance_cm' => 'required_with:bin_1|numeric',
            'bin_1.hx711_raw'   => 'required_with:bin_1|numeric',
            'bin_1.mq_raw'      => 'required_with:bin_1|numeric|min:0',
            'bin_2'             => 'nullable|array',
            'bin_2.distance_cm' => 'required_with:bin_2|numeric',
            'bin_2.hx711_raw'   => 'required_with:bin_2|numeric',
            'bin_2.mq_raw'      => 'required_with:bin_2|numeric|min:0',
        ]);

        $parentDeviceId = $validated['device_id'];

        // 2. Derive battery percentage (shared between both bins)
        $batteryVoltage = null;
        if (!empty($validated['battery_voltage'])) {
            $batteryVoltage = (float) $validated['battery_voltage'];
        } elseif (!empty($validated['battery_adc'])) {
            $batteryVoltage = $this->deriveBatteryVoltageFromAdc((int) $validated['battery_adc']);
        }

        $batteryPercent = $batteryVoltage !== null
            ? $this->deriveBatteryPercent($batteryVoltage)
            : null;

        Log::info("ESP32 Data Received", [
            'device_id'       => $parentDeviceId,
            'battery_voltage' => $batteryVoltage !== null ? round($batteryVoltage, 3) : null,
            'battery_percent' => $batteryPercent,
            'has_bin_data'    => isset($validated['bin_1']),
        ]);

        // 3a. Battery-only payload (no bin data) — just refresh battery on known devices
        if (empty($validated['bin_1']) && empty($validated['bin_2'])) {
            if ($batteryPercent !== null) {
                Device::query()
                    ->where('parent_device_id', $parentDeviceId)
                    ->update([
                        'battery_percent' => $batteryPercent,
                        'last_seen_at'    => now(),
                    ]);
            }

            return response()->json([
                'status'          => 'ok',
                'message'         => 'Battery updated',
                'device_id'       => $parentDeviceId,
                'battery_voltage' => $batteryVoltage !== null ? round($batteryVoltage, 3) : null,
                'battery_percent' => $batteryPercent !== null ? round($batteryPercent, 1) : null,
            ]);
        }

        // 3b. Full sensor payload — process each bin that was sent
        $results = [];
        foreach (['bin_1' => 1, 'bin_2' => 2] as $binKey => $binNumber) {
            if (empty($validated[$binKey])) {
                continue;
            }

            $result = $this->processBinData(
                $parentDeviceId,
                $binNumber,
                $validated[$binKey],
                $batteryPercent ?? 0
            );

            $results["bin_{$binNumber}"] = $result;
        }

        return response()->json([
            'status'          => 'ok',
            'message'         => 'Data received and processed',
            'device_id'       => $parentDeviceId,
            'battery_voltage' => $batteryVoltage !== null ? round($batteryVoltage, 3) : null,
            'battery_percent' => $batteryPercent !== null ? round($batteryPercent, 1) : null,
            'bins'            => $results,
        ]);
    }

    /**
     * Process data for a single bin
     */
    private function processBinData(string $parentDeviceId, int $binNumber, array $binData, float $batteryPercent): array
    {
        $binUid = "{$parentDeviceId}_bin{$binNumber}";
        $device = $this->resolveBinDevice($parentDeviceId, $binNumber, $binUid);

        $metadataUpdates = $this->defaultLocationUpdates($device, $binNumber);

        $device->update([
            ...$metadataUpdates,
            'uid' => $binUid,
            'name' => $device->name ?: "Bin #{$binNumber}",
            'parent_device_id' => $parentDeviceId,
            'bin_number' => $binNumber,
            'is_active' => true,
            'last_seen_at' => now(),
            'battery_percent' => $batteryPercent,
        ]);

        $this->removeSupersededDemoSlot($device);

        // Derive values from raw sensor data
        $fillPercent = $this->deriveFillPercent($binData['distance_cm'], $binNumber);
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

    private function resolveBinDevice(string $parentDeviceId, int $binNumber, string $binUid): Device
    {
        $device = Device::query()
            ->where('parent_device_id', $parentDeviceId)
            ->where('bin_number', $binNumber)
            ->first();

        if ($device) {
            return $device;
        }

        $device = Device::query()
            ->where('uid', $binUid)
            ->first();

        if ($device) {
            return $device;
        }

        return Device::create([
            'uid' => $binUid,
            'name' => "Bin #{$binNumber}",
            'location' => 'Unassigned Location',
            'parent_device_id' => $parentDeviceId,
            'bin_number' => $binNumber,
            'is_active' => true,
        ]);
    }

    private function removeSupersededDemoSlot(Device $device): void
    {
        $defaultUid = Device::defaultUidForBin($device->bin_number);

        if (!$defaultUid) {
            return;
        }

        Device::query()
            ->whereKeyNot($device->id)
            ->whereNull('parent_device_id')
            ->where('bin_number', $device->bin_number)
            ->where('uid', $defaultUid)
            ->delete();
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
     * Formula: fill% = (empty_distance - actual_distance) / (empty_distance - full_distance) * 100
     *
     * Calibration values:
     * - Bin 1: empty = 58.7 cm, full = 10.0 cm
     * - Bin 2: empty = 48.3 cm, full = 10.0 cm
     */
    private function deriveFillPercent(float $distanceCm, int $binNumber): float
    {
        // Handle sensor error (returns -1)
        if ($distanceCm < 0) {
            Log::warning("Ultrasonic sensor error: negative distance", [
                'bin' => $binNumber,
                'distance' => $distanceCm,
            ]);
            return 0;
        }

        $defaults = $binNumber === 1
            ? ['empty' => 58.7, 'full' => 10.0]
            : ['empty' => 48.3, 'full' => 10.0];

        $emptyDistance = config("sensors.ultrasonic.bin_{$binNumber}.empty_distance_cm", $defaults['empty']);
        $fullDistance = config("sensors.ultrasonic.bin_{$binNumber}.full_distance_cm", $defaults['full']);

        // Avoid division by zero
        if ($emptyDistance <= $fullDistance) {
            Log::error("Invalid ultrasonic calibration: empty_distance must be > full_distance", [
                'bin' => $binNumber,
                'empty_distance' => $emptyDistance,
                'full_distance' => $fullDistance,
            ]);
            return 0;
        }

        $percent = (($emptyDistance - $distanceCm) / ($emptyDistance - $fullDistance)) * 100;
        
        // Clamp between 0 and 100
        return max(0, min(100, $percent));
    }

    /**
     * Derive weight in kg from HX711 tared value
     *
     * The firmware sends scale.get_value(times) which is already tare-subtracted
     * (i.e. 0 on an empty bin). So the backend must NOT apply raw_empty again.
     *
     * Formula: weight_kg = hx711_tared / scale_raw_per_gram / 1000
     *
     * Calibration: both bins use SCALE = 87.1 raw/g (matching real firmware)
     *
     * @param float $hx711Raw Tared HX711 reading (get_value output)
     * @param int   $binNumber 1 or 2 to select correct calibration
     */
    private function deriveWeight(float $hx711Raw, int $binNumber): float
    {
        $maxWeight = config('sensors.max_weight_kg', 20.0);

        // Guard: if the raw tared value is near zero the sensor is empty or
        // disconnected — skip the math and return 0 to avoid division noise.
        if (abs($hx711Raw) < 500) {
            return 0.0;
        }

        // Both bins use 87.1 raw/g to match real firmware SCALE1=SCALE2=87.1
        $scale = config("sensors.load_cell.bin_{$binNumber}.scale_raw_per_gram", 87.1);

        if ($scale == 0) {
            Log::error("Invalid weight calibration: scale factor is zero", ['bin' => $binNumber]);
            return 0.0;
        }

        // Firmware value is already tared: weight = taredRaw / scale / 1000
        $weight = ($hx711Raw / $scale) / 1000;

        Log::debug("Weight calculation", [
            'bin'                => $binNumber,
            'hx711_tared_raw'    => $hx711Raw,
            'scale_raw_per_gram' => $scale,
            'calculated_kg'      => $weight,
        ]);

        return max(0.0, min($maxWeight, $weight));
    }

    /**
     * Derive gas level from MQ sensor raw value
     * 
     * Calibration values from Raw Values = Calibration.docx:
     * - Normal air: 100-499
     * - Dangerous (flammable): 500+
     * 
     * Returns: 0 = Normal, 1 = Dangerous
     */
    private function deriveGasLevel(int $mqRaw): int
    {
        $dangerousMin = config('sensors.mq_dangerous_min', 500);

        // Dangerous: At or above threshold
        if ($mqRaw >= $dangerousMin) {
            return 1; // Dangerous
        }

        // Normal: Below threshold
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
        if ($gasLevel >= 1) {
            $alert = $device->alerts()->firstOrCreate(
                ['type' => 'gas_leak', 'status' => 'active'],
                ['message' => 'Dangerous gas level detected! Possible flammable gas.']
            );
            if ($alert->wasRecentlyCreated) {
                event(new AlertCreated($alert));
            }
        } else {
            $device->alerts()
                ->where('type', 'gas_leak')
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

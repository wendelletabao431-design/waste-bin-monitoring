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
            'battery_voltage' => $batteryVoltage !== null ? round($batteryVoltage, 1) : null,
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
        $this->checkWeightAlerts($device, $weightKg, $binNumber);
        $this->checkBatteryHealth($device, $batteryPercent, $fillPercent, $weightKg, $gasLevel);

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
            'fill_percent' => round($fillPercent, 1),
            'weight_kg'    => round($weightKg, 1),
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

        // Treat any reading above 58cm as empty bin (out-of-range)
        if ($distanceCm > 58) {
            return 0;
        }

        $defaults = $binNumber === 1
            ? ['empty' => 58.0, 'full' => 10.0]
            : ['empty' => 48.0, 'full' => 10.0];

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
     * The firmware sends scale.get_value(times), which is the tare-subtracted
     * raw ADC value (≈ 0 for empty bin, positive for weight).
     *
     * Calibration from "Raw Values = Calibration.docx":
     *   Bin 1: SCALE1 = 119,800 raw/kg  (RAW_empty=451,977  | 1.31kg → Δ156,600)
     *   Bin 2: SCALE2 = 117,786 raw/kg  (RAW_empty=-491,000 | 1.31kg → Δ154,300)
     *
     * Formula: weight_kg = hx711_tared / scale_raw_per_kg
     *   (NO /1000 — scale is already in raw-per-kg, not raw-per-gram)
     *
     * @param float $hx711Raw Tared HX711 reading from get_value()
     * @param int   $binNumber 1 or 2
     */
    private function deriveWeight(float $hx711Raw, int $binNumber): float
    {
        $maxWeight = config("sensors.load_cell.bin_{$binNumber}.max_weight_kg", 20.0);

        // raw=0 means sensor not connected — avoid phantom weight from offset formula
        if ($hx711Raw == 0) {
            return 0.0;
        }

        $scale = config("sensors.load_cell.bin_{$binNumber}.scale_raw_per_kg", 22600.0);

        if ($scale == 0) {
            Log::error("Invalid weight calibration: scale factor is zero", ['bin' => $binNumber]);
            return 0.0;
        }

        // Two firmware styles supported:
        //   - If empty_offset_raw > 0: firmware sends untared raw, use (offset - raw) / scale
        //   - If empty_offset_raw == 0: firmware sends tared raw (near 0 when empty), use raw / scale
        $emptyOffset = config("sensors.load_cell.bin_{$binNumber}.empty_offset_raw", 0.0);
        $weight      = $emptyOffset > 0
            ? ($emptyOffset - $hx711Raw) / $scale
            : $hx711Raw / $scale;

        Log::info("Weight calculation", [
            'bin'          => $binNumber,
            'hx711_raw'    => $hx711Raw,
            'empty_offset' => $emptyOffset,
            'scale'        => $scale,
            'calculated_kg'=> round($weight, 3),
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
     * Check and create fill level alerts.
     *
     * Thresholds: warning ≥ 50%, critical ≥ 90%, auto-resolve < 40%.
     */
    private function checkFillAlerts(Device $device, float $fillValue): void
    {
        $criticalThreshold = config('sensors.fill_critical_threshold', 90);
        $warningThreshold  = config('sensors.fill_warning_threshold', 50);

        if ($fillValue >= $criticalThreshold) {
            $alert = $device->alerts()->firstOrCreate(
                ['type' => 'trash_full', 'status' => 'active'],
                ['message' => "Bin is critical ({$fillValue}% full)"]
            );
            // Fire immediately on first create, then re-notify every 5 minutes
            if ($alert->wasRecentlyCreated || $alert->updated_at->diffInMinutes(now()) >= 5) {
                $alert->touch();
                event(new AlertCreated($alert));
            }
        } elseif ($fillValue >= $warningThreshold) {
            $alert = $device->alerts()->firstOrCreate(
                ['type' => 'trash_warning', 'status' => 'active'],
                ['message' => "Bin is getting full ({$fillValue}% full)"]
            );
            // Fire immediately on first create, then re-notify every 5 minutes
            if ($alert->wasRecentlyCreated || $alert->updated_at->diffInMinutes(now()) >= 5) {
                $alert->touch();
                event(new AlertCreated($alert));
            }
        } elseif ($fillValue < 40) {
            $device->alerts()
                ->whereIn('type', ['trash_full', 'trash_warning'])
                ->where('status', 'active')
                ->update(['status' => 'resolved']);
        }
    }

    /**
     * Check and create weight alerts.
     *
     * Bin 1: warning ≥ 20 kg, critical ≥ 36 kg (max 40 kg)
     * Bin 2: warning ≥ 10 kg, critical ≥ 18 kg (max 20 kg)
     */
    private function checkWeightAlerts(Device $device, float $weightKg, int $binNumber): void
    {
        $warningKg  = config("sensors.load_cell.bin_{$binNumber}.warning_weight_kg",
            $binNumber === 1 ? 20.0 : 10.0
        );
        $criticalKg = config("sensors.load_cell.bin_{$binNumber}.critical_weight_kg",
            $binNumber === 1 ? 36.0 : 18.0
        );

        if ($weightKg >= $criticalKg) {
            $alert = $device->alerts()->firstOrCreate(
                ['type' => 'weight_critical', 'status' => 'active'],
                ['message' => "Bin weight critical (" . round($weightKg, 1) . " kg)"]
            );
            if ($alert->wasRecentlyCreated) {
                event(new AlertCreated($alert));
            }
        } elseif ($weightKg >= $warningKg) {
            $alert = $device->alerts()->firstOrCreate(
                ['type' => 'weight_warning', 'status' => 'active'],
                ['message' => "Bin weight warning (" . round($weightKg, 1) . " kg)"]
            );
            if ($alert->wasRecentlyCreated) {
                event(new AlertCreated($alert));
            }
        } else {
            $device->alerts()
                ->whereIn('type', ['weight_critical', 'weight_warning'])
                ->where('status', 'active')
                ->update(['status' => 'resolved']);
        }
    }

    /**
     * Check and create gas level alerts
     */
    private function checkGasAlerts(Device $device, int $gasLevel): void
    {
        $dangerousMin = config('sensors.mq_dangerous_min', 500);

        if ($gasLevel >= $dangerousMin) {
            $alert = $device->alerts()->firstOrCreate(
                ['type' => 'gas_leak', 'status' => 'active'],
                ['message' => "Flammable gas detected! MQ raw: {$gasLevel}"]
            );
            // Alert immediately on detection, then re-notify every 5 minutes
            if ($alert->wasRecentlyCreated || $alert->updated_at->diffInMinutes(now()) >= 5) {
                $alert->touch();
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
     * Send a hardware health summary email when battery drops by 10%.
     * Includes battery, fill, weight, and gas status for all components.
     */
    private function checkBatteryHealth(Device $device, float $batteryPercent, float $fillPercent, float $weightKg, int $gasLevel): void
    {
        // Round down to nearest 10% milestone (e.g. 87% → 80)
        $milestone = floor($batteryPercent / 10) * 10;

        // Retrieve last notified milestone stored on the device
        $lastMilestone = (int) ($device->meta['battery_milestone'] ?? 100);

        // Only notify when crossing a new lower milestone
        if ($milestone >= $lastMilestone) return;

        // Save new milestone so we don't re-notify until the next drop
        $device->update(['meta' => array_merge($device->meta ?? [], ['battery_milestone' => $milestone])]);

        $gasStatus   = $gasLevel >= config('sensors.mq_dangerous_min', 500) ? 'DANGEROUS' : 'Normal';
        $battStatus  = $batteryPercent <= 20 ? 'CRITICAL' : ($batteryPercent <= 50 ? 'LOW' : 'OK');

        $alert = $device->alerts()->create([
            'type'    => 'battery_health',
            'status'  => 'active',
            'message' => implode(' | ', [
                "Battery: {$batteryPercent}% ({$battStatus})",
                "Waste Level: {$fillPercent}%",
                "Weight: {$weightKg} kg",
                "Gas: {$gasStatus}",
            ]),
        ]);

        event(new AlertCreated($alert));

        // Auto-resolve immediately — this is a one-shot health report, not an ongoing alert
        $alert->update(['status' => 'resolved']);
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

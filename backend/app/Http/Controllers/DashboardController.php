<?php

namespace App\Http\Controllers;

use App\Models\Device;
use Illuminate\Http\Request;
use Illuminate\Support\Collection;
use Laravel\Sanctum\PersonalAccessToken;

class DashboardController extends Controller
{
    /**
     * Summary Metrics for SystemStatus and Sidebar Badges
     * GET /api/dashboard/summary
     */
    public function summary()
    {
        // Online Check: Device seen in last 5 minutes
        $offlineThreshold = config('sensors.offline_threshold_minutes', 5);
        $onlineThreshold = now()->subMinutes($offlineThreshold);

        $devices = $this->filterVisibleDevices(
            Device::query()
                ->with('alerts')
                ->orderBy('bin_number')
                ->orderBy('id')
                ->get()
        );

        $totalBins = $devices->count();
        $binsOnline = $devices->filter(function (Device $device) use ($onlineThreshold) {
            return $device->last_seen_at && $device->last_seen_at >= $onlineThreshold;
        })->count();

        // System is Offline if ALL devices are offline
        $systemStatus = ($binsOnline > 0) ? 'Online' : 'Offline';

        // Active users: tokens used in last 15 minutes
        $activeUsersThreshold = now()->subMinutes(15);
        $activeUsers = PersonalAccessToken::where('last_used_at', '>=', $activeUsersThreshold)
            ->distinct('tokenable_id')
            ->count('tokenable_id');

        // Active alerts breakdown
        $activeAlerts = $devices->flatMap(function (Device $device) {
            return $device->alerts->where('status', 'active');
        });
        
        $criticalCount = $activeAlerts->whereIn('type', ['gas_leak', 'trash_full', 'weight_critical'])->count();
        $warningCount  = $activeAlerts->whereIn('type', ['gas_elevated', 'trash_warning', 'weight_warning'])->count();
        $normalCount = $totalBins - $devices->filter(function (Device $device) {
            return $device->alerts->where('status', 'active')->isNotEmpty();
        })->count();

        return response()->json([
            'system_status' => $systemStatus,
            'active_users' => $activeUsers,
            'total_bins' => $totalBins,
            'bins_online' => $binsOnline,
            'bins_offline' => $totalBins - $binsOnline,
            'alerts' => [
                'normal' => max(0, $normalCount),
                'warning' => $warningCount,
                'critical' => $criticalCount
            ]
        ]);
    }

    /**
     * List all devices with latest metrics
     * GET /api/devices
     */
    public function devices()
    {
        $offlineThreshold = config('sensors.offline_threshold_minutes', 5);
        
        $devices = $this->filterVisibleDevices(
            Device::query()
                ->orderBy('bin_number')
                ->orderBy('id')
                ->get()
        );

        $data = $devices->map(function (Device $dev) use ($offlineThreshold) {
            ['location' => $location, 'latitude' => $latitude, 'longitude' => $longitude] = $this->resolveMapData($dev);

            // Check if device is online (seen in last X minutes)
            $isOnline = $dev->last_seen_at && 
                        $dev->last_seen_at >= now()->subMinutes($offlineThreshold);

            // CRITICAL: No sensor data = zero values.
            // Query per-type directly per device so readings from another device
            // never contaminate or crowd out this device's values.
            if ($isOnline) {
                $lastFill    = $dev->readings()->where('type', 'fill')->latest()->value('value') ?? 0;
                $lastGas     = $dev->readings()->where('type', 'gas')->latest()->value('value') ?? 0;
                $lastWeight  = $dev->readings()->where('type', 'weight')->latest()->value('value') ?? 0;
                // Use cached battery_percent column first (updated by sendBatteryOnly),
                // fall back to the readings table.
                $lastBattery = $dev->battery_percent
                    ?? ($dev->readings()->where('type', 'battery')->latest()->value('value') ?? 0);
            } else {
                // OFFLINE = All zeros, no fake data
                $lastFill = 0;
                $lastBattery = 0;
                $lastGas = 0;
                $lastWeight = 0;
            }

            // Map gas level (0/1) to display strings
            $gasLabel = match ((int) $lastGas) {
                0       => 'Normal',
                1       => 'Dangerous',
                default => 'Normal'
            };

            // Determine status based on fill level and online state
            $status = 'Offline';
            if ($isOnline) {
                if ($lastFill >= 90) {
                    $status = 'Critical';
                } elseif ($lastFill >= 50) {
                    $status = 'Warning';
                } else {
                    $status = 'Normal';
                }
            }

            return [
                'id' => $dev->id,
                'name' => $dev->name ?? "Bin #{$dev->id}",
                'location' => $location,
                'latitude' => $latitude,
                'longitude' => $longitude,
                'fill'    => round($lastFill, 1),
                'battery' => round($lastBattery, 1),
                'gas'     => $gasLabel,
                'weight'  => round($lastWeight, 1),
                'last_seen' => $dev->last_seen_at?->diffForHumans() ?? 'Never',
                'last_seen_at' => $dev->last_seen_at?->toIso8601String(),
                'status' => $status,
                'bin_number' => $dev->bin_number ?? 1,
                'parent_device_id' => $dev->parent_device_id,
            ];
        });

        return response()->json($data);
    }

    private function filterVisibleDevices(Collection $devices): Collection
    {
        $realBinNumbers = $devices
            ->reject(fn(Device $device) => $device->isDemoPlaceholder())
            ->map(fn(Device $device) => (int) ($device->bin_number ?? 1))
            ->unique();

        if ($realBinNumbers->isEmpty()) {
            return $devices
                ->reject(function (Device $device) {
                    return $device->isDemoPlaceholder()
                        && (int) ($device->bin_number ?? 1) !== 1;
                })
                ->sortBy(fn(Device $device) => sprintf('%03d-%010d', (int) ($device->bin_number ?? 99), $device->id))
                ->values();
        }

        return $devices
            ->reject(function (Device $device) use ($realBinNumbers) {
                return $device->isDemoPlaceholder()
                    && $realBinNumbers->contains((int) ($device->bin_number ?? 1));
            })
            ->sortBy(fn(Device $device) => sprintf('%03d-%010d', (int) ($device->bin_number ?? 99), $device->id))
            ->values();
    }

    private function resolveMapData(Device $device): array
    {
        $defaults = Device::defaultMetadataForBin($device->bin_number);

        return [
            'location' => $device->hasAssignedLocation()
                ? $device->location
                : ($defaults['location'] ?? ($device->location ?? 'Unknown')),
            'latitude' => $device->hasAssignedCoordinates()
                ? $device->latitude
                : ($defaults['latitude'] ?? $device->latitude),
            'longitude' => $device->hasAssignedCoordinates()
                ? $device->longitude
                : ($defaults['longitude'] ?? $device->longitude),
        ];
    }

    /**
     * Detailed metrics for a specific device
     * GET /api/devices/{id}/details
     */
    public function deviceDetails($id)
    {
        $device = Device::findOrFail($id);
        $offlineThreshold = config('sensors.offline_threshold_minutes', 5);
        
        $isOnline = $device->last_seen_at && 
                    $device->last_seen_at >= now()->subMinutes($offlineThreshold);

        // =================================================================
        // EFFICIENCY: Fill Stability Efficiency (Based on Real Data)
        // =================================================================
        // Efficiency = 100 - (normalized standard deviation of fill levels)
        // Lower variance = higher efficiency (stable fill pattern)
        // 
        // This metric represents how predictably the bin fills,
        // allowing for better collection scheduling.
        // =================================================================
        
        $efficiency = $this->calculateFillStabilityEfficiency($device, $isOnline);

        // =================================================================
        // FILL RATE: Change in fill level over last 24 hours
        // =================================================================
        
        $fillRate = $this->calculateFillRate($device, $isOnline);

        // =================================================================
        // COLLECTIONS: Number of emptying events this week
        // =================================================================
        
        $collectionsWeek = $device->collections()
            ->where('collected_at', '>=', now()->subWeek())
            ->count();

        // =================================================================
        // CAPACITY: Remaining space in the bin
        // Combined fill (volume) + weight capacity, averaged.
        // fill_capacity   = 100 - fill%
        // weight_capacity = 100 - (weight_kg / max_weight_kg * 100)
        // capacity        = average of both, clamped 0-100
        // =================================================================

        $binNumber   = $device->bin_number ?? 1;
        $maxWeightKg = config("sensors.load_cell.bin_{$binNumber}.max_weight_kg", 20.0);

        $currentFill   = 0.0;
        $currentWeight = 0.0;

        if ($isOnline) {
            $currentFill = $device->readings()
                ->where('type', 'fill')
                ->latest()
                ->first()?->value ?? 0;

            $currentWeight = $device->readings()
                ->where('type', 'weight')
                ->latest()
                ->first()?->value ?? 0;
        }

        $fillCapacity   = max(0, 100 - $currentFill);
        $weightCapacity = max(0, 100 - ($currentWeight / $maxWeightKg * 100));
        $capacity       = round(($fillCapacity + $weightCapacity) / 2, 1);

        return response()->json([
            'fill_rate' => $fillRate,
            'last_updated' => $device->last_seen_at?->diffForHumans() ?? 'Never',
            'last_updated_at' => $device->last_seen_at?->toIso8601String(),
            'collections_week' => $collectionsWeek,
            'efficiency'     => round($efficiency, 1) . '%',
            'efficiency_raw' => round($efficiency, 1),
            'capacity' => round($capacity, 1),
            'is_online' => $isOnline,
            'bin_number' => $device->bin_number ?? 1,
            'battery_percent' => $device->battery_percent ?? 0,
        ]);
    }

    /**
     * Calculate Fill Stability Efficiency
     * 
     * Based on the standard deviation of fill readings over the past 7 days.
     * Lower variance = more predictable fill pattern = higher efficiency
     * 
     * Formula: Efficiency = 100 - (stdDev * 2)
     * - StdDev of 0 = 100% efficiency (perfectly stable)
     * - StdDev of 50 = 0% efficiency (chaotic fill pattern)
     * 
     * @param Device $device
     * @param bool $isOnline
     * @return float Efficiency percentage (0-100)
     */
    private function calculateFillStabilityEfficiency(Device $device, bool $isOnline): float
    {
        // No data or offline = 0% efficiency (as per requirements)
        if (!$isOnline) {
            return 0;
        }

        // Get fill readings from the past 7 days
        $readings = $device->readings()
            ->where('type', 'fill')
            ->where('created_at', '>=', now()->subDays(7))
            ->pluck('value');

        // No historical data = 0% efficiency
        if ($readings->isEmpty() || $readings->count() < 2) {
            return 0;
        }

        // Calculate mean
        $mean = $readings->avg();

        // Calculate variance
        $variance = $readings->map(function ($value) use ($mean) {
            return pow($value - $mean, 2);
        })->avg();

        // Calculate standard deviation
        $stdDev = sqrt($variance);

        // Convert to efficiency score
        // Max theoretical StdDev is ~50 (if values swing between 0 and 100)
        // We normalize: efficiency = 100 - (stdDev * 2)
        $efficiency = 100 - ($stdDev * 2);

        // Clamp between 0 and 100
        return max(0, min(100, $efficiency));
    }

    /**
     * Calculate Fill Rate (change over last 24 hours)
     * 
     * @param Device $device
     * @param bool $isOnline
     * @return string Formatted fill rate string
     */
    private function calculateFillRate(Device $device, bool $isOnline): string
    {
        if (!$isOnline) {
            return '0%';
        }

        // Current fill level
        $currentFill = $device->readings()
            ->where('type', 'fill')
            ->latest()
            ->first()?->value ?? 0;

        // Fill level 24 hours ago
        $yesterdayFill = $device->readings()
            ->where('type', 'fill')
            ->where('created_at', '<=', now()->subDay())
            ->latest()
            ->first()?->value ?? 0;

        $fillRate = $currentFill - $yesterdayFill;

        // Format with sign
        $sign = $fillRate > 0 ? '+' : '';
        return "{$sign}" . round($fillRate, 1) . '%';
    }

    /**
     * Get all devices grouped by parent device (ESP32)
     * GET /api/devices/grouped
     */
    public function devicesGrouped()
    {
        $devices = Device::whereNotNull('parent_device_id')
            ->with(['readings' => fn($q) => $q->latest()->limit(4)])
            ->get()
            ->groupBy('parent_device_id');

        $result = $devices->map(function ($bins, $parentId) {
            return [
                'parent_device_id' => $parentId,
                'bins' => $bins->map(fn($bin) => [
                    'id' => $bin->id,
                    'name' => $bin->name,
                    'bin_number' => $bin->bin_number,
                    'fill' => $bin->readings->where('type', 'fill')->first()?->value ?? 0,
                    'battery' => $bin->battery_percent ?? 0,
                    'last_seen' => $bin->last_seen_at?->diffForHumans() ?? 'Never',
                ]),
            ];
        })->values();

        return response()->json($result);
    }

    /**
     * Get historical readings for a device
     * GET /api/devices/{id}/history
     */
    public function deviceHistory($id, Request $request)
    {
        $device = Device::findOrFail($id);
        
        $type = $request->get('type', 'fill'); // fill, weight, gas, battery
        $days = min(30, max(1, (int) $request->get('days', 7)));

        $readings = $device->readings()
            ->where('type', $type)
            ->where('created_at', '>=', now()->subDays($days))
            ->orderBy('created_at', 'asc')
            ->get(['value', 'raw_value', 'created_at']);

        return response()->json([
            'device_id' => $device->id,
            'type' => $type,
            'days' => $days,
            'readings' => $readings->map(fn($r) => [
                'value' => $r->value,
                'raw_value' => $r->raw_value,
                'timestamp' => $r->created_at->toIso8601String(),
            ]),
        ]);
    }
}

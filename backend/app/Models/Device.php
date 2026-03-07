<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\HasMany;

class Device extends Model
{
    private const UNASSIGNED_LOCATIONS = [
        '',
        'unassigned location',
        'unknown',
    ];

    protected $fillable = [
        'uid', 
        'name', 
        'location', 
        'latitude',
        'longitude',
        'api_key', 
        'last_seen_at', 
        'is_active',
        'parent_device_id',
        'bin_number',
        'battery_percent',
        'power_source',
    ];

    protected $casts = [
        'last_seen_at' => 'datetime',
        'is_active' => 'boolean',
        'latitude' => 'float',
        'longitude' => 'float',
        'battery_percent' => 'float',
    ];

    public static function defaultMetadataForBin(?int $binNumber): array
    {
        return match ((int) $binNumber) {
            1 => [
                'location' => 'Cafeteria, Building A-1',
                'latitude' => 11.237934,
                'longitude' => 124.999284,
            ],
            2 => [
                'location' => 'Library Entrance',
                'latitude' => 11.210018,
                'longitude' => 124.990660,
            ],
            default => [
                'location' => null,
                'latitude' => null,
                'longitude' => null,
            ],
        };
    }

    public function hasAssignedLocation(): bool
    {
        return !in_array(strtolower(trim((string) $this->location)), self::UNASSIGNED_LOCATIONS, true);
    }

    public function hasAssignedCoordinates(): bool
    {
        return is_numeric($this->latitude)
            && is_numeric($this->longitude)
            && (float) $this->latitude !== 0.0
            && (float) $this->longitude !== 0.0;
    }

    public function readings(): HasMany
    {
        return $this->hasMany(SensorReading::class);
    }

    public function alerts(): HasMany
    {
        return $this->hasMany(Alert::class);
    }

    public function commands(): HasMany
    {
        return $this->hasMany(Command::class);
    }

    public function collections(): HasMany
    {
        return $this->hasMany(Collection::class);
    }
}

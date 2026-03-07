<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class SensorReading extends Model
{
    protected $fillable = ['device_id', 'type', 'value', 'unit', 'raw_value'];

    public function device(): BelongsTo
    {
        return $this->belongsTo(Device::class);
    }
}

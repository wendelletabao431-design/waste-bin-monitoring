<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class Alert extends Model
{
    protected $fillable = ['device_id', 'type', 'message', 'status'];

    public function device(): BelongsTo
    {
        return $this->belongsTo(Device::class);
    }
}

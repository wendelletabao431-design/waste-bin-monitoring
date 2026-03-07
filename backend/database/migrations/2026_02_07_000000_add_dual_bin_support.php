<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    /**
     * Run the migrations.
     * 
     * Adds support for dual-bin ESP32 devices where one physical device
     * controls two separate bins. Each bin is stored as a separate Device
     * record but linked via parent_device_id.
     */
    public function up(): void
    {
        Schema::table('devices', function (Blueprint $table) {
            // Links bins to their parent ESP32 device (MAC address)
            $table->string('parent_device_id')->nullable()->after('uid');
            
            // Bin number (1 or 2) for dual-bin devices
            $table->tinyInteger('bin_number')->default(1)->after('parent_device_id');
            
            // Cached battery percentage for quick access
            $table->float('battery_percent')->nullable()->after('is_active');
            
            // Power source indicator
            $table->string('power_source')->nullable()->after('battery_percent');
            
            // Index for querying bins by parent device
            $table->index('parent_device_id');
        });

        // Add bin_number to sensor_readings for more granular tracking
        Schema::table('sensor_readings', function (Blueprint $table) {
            // Store raw sensor value alongside derived value
            $table->float('raw_value')->nullable()->after('value');
        });

        // Add bin_number to collections table
        Schema::table('collections', function (Blueprint $table) {
            // Previous fill level before collection
            $table->float('previous_fill')->nullable()->after('amount_collected');
        });
    }

    /**
     * Reverse the migrations.
     */
    public function down(): void
    {
        Schema::table('devices', function (Blueprint $table) {
            $table->dropIndex(['parent_device_id']);
            $table->dropColumn(['parent_device_id', 'bin_number', 'battery_percent', 'power_source']);
        });

        Schema::table('sensor_readings', function (Blueprint $table) {
            $table->dropColumn('raw_value');
        });

        Schema::table('collections', function (Blueprint $table) {
            $table->dropColumn('previous_fill');
        });
    }
};

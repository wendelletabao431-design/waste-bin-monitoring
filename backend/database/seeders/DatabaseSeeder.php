<?php

namespace Database\Seeders;

use App\Models\User;
use App\Models\Device;
use App\Models\SensorReading;
use App\Models\Collection;
// use Illuminate\Database\Console\Seeds\WithoutModelEvents;
use Illuminate\Database\Seeder;

class DatabaseSeeder extends Seeder
{
    /**
     * Seed the application's database.
     */
    public function run(): void
    {
        User::firstOrCreate([
            'email' => 'admin@example.com',
        ], [
            'name' => 'Admin User',
            'password' => 'password',
        ]);

        $device1 = Device::updateOrCreate([
            'uid' => 'ESP32_001',
        ], [
            'name' => 'Bin #1',
            'location' => 'Cafeteria, Building A-1',
            'latitude' => 11.237934,
            'longitude' => 124.999284,
            'api_key' => 'sk_test_123',
            'last_seen_at' => now(),
            'is_active' => true,
            'battery_percent' => 87,
            'bin_number' => 1,
        ]);

        $device2 = Device::updateOrCreate([
            'uid' => 'ESP32_002',
        ], [
            'name' => 'Bin #2',
            'location' => 'Library Entrance',
            'latitude' => 11.210018,
            'longitude' => 124.990660,
            'api_key' => 'sk_test_456',
            'last_seen_at' => now()->subMinutes(10),
            'is_active' => true,
            'battery_percent' => 62,
            'bin_number' => 2,
        ]);

        $this->seedHistoryIfMissing($device1, 'normal');
        $this->seedHistoryIfMissing($device2, 'full');
    }

    private function seedHistoryIfMissing(Device $device, string $pattern): void
    {
        if ($device->readings()->exists()) {
            return;
        }

        $start = now()->subDay();

        $currentFill = 0;

        for ($i = 0; $i < 50; $i++) {
            $time = $start->copy()->addMinutes($i * 30);

            if ($pattern === 'normal') {
                $currentFill += rand(0, 5);
            } else {
                $currentFill += rand(2, 8);
            }

            if ($currentFill > 100) {
                Collection::create([
                    'device_id' => $device->id,
                    'collected_at' => $time,
                    'amount_collected' => 100,
                ]);
                $currentFill = 0;
            }

            SensorReading::create([
                'device_id' => $device->id,
                'type' => 'fill',
                'value' => $currentFill,
                'unit' => '%',
                'created_at' => $time,
                'updated_at' => $time,
            ]);

            SensorReading::create([
                'device_id' => $device->id,
                'type' => 'battery',
                'value' => max(100 - ($i * 0.5), 20),
                'unit' => '%',
                'created_at' => $time,
                'updated_at' => $time,
            ]);

            SensorReading::create([
                'device_id' => $device->id,
                'type' => 'gas',
                'value' => ($currentFill > 80) ? 1 : 0,
                'unit' => 'level',
                'created_at' => $time,
                'updated_at' => $time,
            ]);
        }
    }
}

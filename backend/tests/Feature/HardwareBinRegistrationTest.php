<?php

namespace Tests\Feature;

use App\Models\Device;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\TestCase;

class HardwareBinRegistrationTest extends TestCase
{
    use RefreshDatabase;

    public function test_devices_endpoint_only_returns_bin_one_when_only_demo_bins_exist(): void
    {
        $this->createDemoDevice(1);
        $this->createDemoDevice(2);

        $response = $this->getJson('/api/devices');

        $response->assertOk();
        $response->assertJsonCount(1);
        $response->assertJsonPath('0.bin_number', 1);
    }

    public function test_hardware_sync_replaces_demo_bins_instead_of_creating_duplicates(): void
    {
        $this->createDemoDevice(1);
        $this->createDemoDevice(2);

        $response = $this->postJson('/api/bin-data', $this->hardwarePayload());

        $response->assertOk();

        $this->assertDatabaseCount('devices', 2);
        $this->assertDatabaseHas('devices', [
            'uid' => 'HW-001_bin1',
            'parent_device_id' => 'HW-001',
            'bin_number' => 1,
        ]);
        $this->assertDatabaseHas('devices', [
            'uid' => 'HW-001_bin2',
            'parent_device_id' => 'HW-001',
            'bin_number' => 2,
        ]);
        $this->assertDatabaseMissing('devices', ['uid' => Device::defaultUidForBin(1)]);
        $this->assertDatabaseMissing('devices', ['uid' => Device::defaultUidForBin(2)]);
    }

    public function test_hardware_sync_removes_stale_demo_bins_when_real_bins_already_exist(): void
    {
        $this->createDemoDevice(1);
        $this->createDemoDevice(2);
        $this->createRealHardwareBin(1);
        $this->createRealHardwareBin(2);

        $response = $this->postJson('/api/bin-data', $this->hardwarePayload());

        $response->assertOk();

        $this->assertDatabaseCount('devices', 2);
        $this->assertDatabaseMissing('devices', ['uid' => Device::defaultUidForBin(1)]);
        $this->assertDatabaseMissing('devices', ['uid' => Device::defaultUidForBin(2)]);

        $devicesResponse = $this->getJson('/api/devices');

        $devicesResponse->assertOk();
        $devicesResponse->assertJsonCount(2);
        $devicesResponse->assertJsonPath('0.bin_number', 1);
        $devicesResponse->assertJsonPath('1.bin_number', 2);
    }

    private function createDemoDevice(int $binNumber): void
    {
        $metadata = Device::defaultMetadataForBin($binNumber);

        Device::create([
            'uid' => Device::defaultUidForBin($binNumber),
            'name' => "Bin #{$binNumber}",
            'location' => $metadata['location'],
            'latitude' => $metadata['latitude'],
            'longitude' => $metadata['longitude'],
            'is_active' => true,
            'bin_number' => $binNumber,
        ]);
    }

    private function createRealHardwareBin(int $binNumber): void
    {
        $metadata = Device::defaultMetadataForBin($binNumber);

        Device::create([
            'uid' => "HW-001_bin{$binNumber}",
            'name' => "Bin #{$binNumber}",
            'location' => $metadata['location'],
            'latitude' => $metadata['latitude'],
            'longitude' => $metadata['longitude'],
            'is_active' => true,
            'parent_device_id' => 'HW-001',
            'bin_number' => $binNumber,
            'last_seen_at' => now(),
        ]);
    }

    private function hardwarePayload(): array
    {
        return [
            'device_id' => 'HW-001',
            'battery_voltage' => 11.9,
            'bin_1' => [
                'distance_cm' => 42.5,
                'hx711_raw' => 552000,
                'mq_raw' => 180,
            ],
            'bin_2' => [
                'distance_cm' => 38.1,
                'hx711_raw' => -410000,
                'mq_raw' => 220,
            ],
        ];
    }
}

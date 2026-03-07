<?php

namespace App\Http\Controllers;

use App\Models\User;
use App\Models\Device;
use App\Models\Alert;
use App\Notifications\AlertNotification;
use Illuminate\Support\Facades\Config;
use Illuminate\Support\Facades\Log;

class TestController extends Controller
{
    public function testAlerts()
    {
        $results = [
            'timestamp' => now()->toISOString(),
            'users' => [],
            'devices' => [],
            'config' => [
                'queue_connection' => config('queue.default'),
                'mail_mailer' => config('mail.default'),
                'mail_host' => config('mail.mailers.smtp.host'),
                'mail_port' => config('mail.mailers.smtp.port'),
                'mail_username' => config('mail.mailers.smtp.username'),
                'mail_from_address' => config('mail.from.address'),
            ],
            'test_alert' => null,
            'emails_sent' => 0,
            'emails_failed' => 0,
            'recipients' => [],
            'message' => '',
        ];

        $users = User::where('notification_enabled', true)->get();
        foreach ($users as $user) {
            $results['users'][] = [
                'id' => $user->id,
                'name' => $user->name,
                'email' => $user->email,
                'notification_enabled' => $user->notification_enabled,
            ];
        }

        $devices = Device::all();
        $results['devices'] = $devices->map(function ($device) {
            return [
                'id' => $device->id,
                'uid' => $device->uid,
                'name' => $device->name,
                'location' => $device->location,
                'last_seen_at' => $device->last_seen_at,
            ];
        })->toArray();
        $results['devices_count'] = $devices->count();

        if ($devices->isEmpty()) {
            $device = Device::create([
                'uid' => 'TEST-DEVICE-' . rand(1000, 9999),
                'name' => 'Test Trash Bin',
                'location' => 'Test Location',
                'fill_level' => 0,
                'gas_level' => 0,
                'battery_level' => 100,
                'last_seen_at' => now(),
            ]);
            $results['devices'][] = [
                'id' => $device->id,
                'uid' => $device->uid,
                'name' => $device->name,
                'location' => $device->location,
                'last_seen_at' => $device->last_seen_at,
            ];
            $results['devices_count'] = 1;
            $results['message'] .= 'Created test device. ';
        } else {
            $device = $devices->first();
        }

        $existingTestAlert = Alert::where('message', 'like', '%TEST ALERT%')->first();
        if ($existingTestAlert) {
            $alert = $existingTestAlert;
            $results['test_alert'] = [
                'id' => $alert->id,
                'type' => $alert->type,
                'message' => $alert->message,
                'created' => false,
                'note' => 'Using existing test alert (not creating new one)',
            ];
        } else {
            $alert = Alert::create([
                'device_id' => $device->id,
                'type' => 'trash_warning',
                'message' => 'TEST ALERT: This is a test alert to verify email notifications.',
                'status' => 'active',
            ]);
            $results['test_alert'] = [
                'id' => $alert->id,
                'type' => $alert->type,
                'message' => $alert->message,
                'created' => true,
            ];
        }

        if ($users->isNotEmpty()) {
            foreach ($users as $user) {
                try {
                    $user->notify(new AlertNotification($alert));
                    $results['emails_sent']++;
                    $results['recipients'][] = [
                        'email' => $user->email,
                        'status' => 'sent',
                    ];
                } catch (\Exception $e) {
                    $results['emails_failed']++;
                    $results['recipients'][] = [
                        'email' => $user->email,
                        'status' => 'failed',
                        'error' => $e->getMessage(),
                    ];
                    Log::error('Test alert email failed', [
                        'email' => $user->email,
                        'error' => $e->getMessage()
                    ]);
                }
            }
            $results['message'] .= "Sent {$results['emails_sent']} email(s). Check your inbox (and spam folder).";
            if ($results['emails_failed'] > 0) {
                $results['message'] .= " {$results['emails_failed']} email(s) failed.";
            }
        } else {
            $results['message'] .= 'No users with notifications enabled. Enable notifications in profile settings.';
        }

        return response()->json($results);
    }

    public function cleanup()
    {
        $deletedAlerts = Alert::where('message', 'like', '%TEST ALERT%')->count();
        $deletedDevices = Device::where('uid', 'like', 'TEST-DEVICE-%')->count();
        
        Alert::where('message', 'like', '%TEST ALERT%')->delete();
        Device::where('uid', 'like', 'TEST-DEVICE-%')->delete();

        return response()->json([
            'message' => 'Test data cleaned up successfully',
            'deleted_alerts' => $deletedAlerts,
            'deleted_devices' => $deletedDevices,
        ]);
    }
}

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

    public function diagnoseSmtp()
    {
        $host = config('mail.mailers.smtp.host');
        $port = (int) config('mail.mailers.smtp.port');
        $username = config('mail.mailers.smtp.username');
        $password = config('mail.mailers.smtp.password');
        $encryption = config('mail.mailers.smtp.scheme') ?? env('MAIL_ENCRYPTION');
        $fromAddress = config('mail.from.address');

        $result = [
            'timestamp' => now()->toISOString(),
            'config_read_from_container' => [
                'host' => $host,
                'port' => $port,
                'username' => $username,
                'password_length' => strlen((string) $password),
                'password_first_4' => substr((string) $password, 0, 4),
                'encryption' => $encryption,
                'from_address' => $fromAddress,
                'mail_timeout_env' => env('MAIL_TIMEOUT'),
            ],
            'step_1_dns_lookup' => null,
            'step_2_tcp_connect' => null,
            'step_3_smtp_send' => null,
        ];

        // STEP 1: DNS lookup — can Railway even resolve smtp.gmail.com?
        $ip = @gethostbyname($host);
        $result['step_1_dns_lookup'] = [
            'host' => $host,
            'resolved_ip' => $ip,
            'success' => $ip !== $host,
        ];

        // STEP 2: Raw TCP socket — is outbound SMTP blocked, or is it auth?
        $start = microtime(true);
        $errno = 0;
        $errstr = '';
        $socket = @fsockopen($host, $port, $errno, $errstr, 10); // 10s timeout
        $elapsed = round((microtime(true) - $start) * 1000);

        if ($socket) {
            $banner = @fgets($socket, 1024);
            @fclose($socket);
            $result['step_2_tcp_connect'] = [
                'success' => true,
                'elapsed_ms' => $elapsed,
                'banner' => trim((string) $banner),
                'conclusion' => 'Railway CAN reach ' . $host . ':' . $port . '. Network is fine. If email still fails, it is an auth issue (wrong app password, 2FA off, or new account restricted).',
            ];
        } else {
            $result['step_2_tcp_connect'] = [
                'success' => false,
                'elapsed_ms' => $elapsed,
                'errno' => $errno,
                'errstr' => $errstr,
                'conclusion' => 'Railway CANNOT reach ' . $host . ':' . $port . '. Outbound SMTP is blocked on this network — switch to Brevo, Resend, Mailgun, or another API-based provider.',
            ];
        }

        // STEP 3: Only try real send if TCP worked
        if ($socket === false) {
            $result['step_3_smtp_send'] = ['skipped' => true, 'reason' => 'TCP connect failed, skipping SMTP send'];
            return response()->json($result);
        }

        try {
            \Illuminate\Support\Facades\Mail::raw('SMTP diagnostic test at ' . now(), function ($msg) use ($fromAddress) {
                $msg->to($fromAddress)->subject('SMTP Diagnostic Test');
            });
            $result['step_3_smtp_send'] = ['success' => true, 'message' => 'Test email sent successfully to ' . $fromAddress];
        } catch (\Throwable $e) {
            $result['step_3_smtp_send'] = [
                'success' => false,
                'exception_class' => get_class($e),
                'message' => $e->getMessage(),
                'previous' => $e->getPrevious() ? [
                    'class' => get_class($e->getPrevious()),
                    'message' => $e->getPrevious()->getMessage(),
                ] : null,
            ];
        }

        return response()->json($result);
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

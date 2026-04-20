<?php

namespace App\Http\Controllers;

use App\Models\User;
use App\Models\Device;
use App\Models\Alert;
use App\Events\AlertCreated;
use App\Notifications\AlertNotification;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Config;
use Illuminate\Support\Facades\Http;
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

    public function diagnoseBrevoApi()
    {
        $apiKey      = config('services.brevo.api_key');
        $fromAddress = config('mail.from.address');
        $fromName    = config('mail.from.name');

        $result = [
            'timestamp' => now()->toISOString(),
            'config' => [
                'brevo_api_key_set'    => !empty($apiKey),
                'brevo_api_key_length' => strlen((string) $apiKey),
                'brevo_api_key_prefix' => substr((string) $apiKey, 0, 8),
                'from_address'         => $fromAddress,
                'from_name'            => $fromName,
            ],
            'step_1_account_check' => null,
            'step_2_send_test'     => null,
        ];

        if (empty($apiKey)) {
            $result['error'] = 'BREVO_API_KEY is not set in Railway environment variables.';
            return response()->json($result);
        }

        // STEP 1: Check API key validity by fetching account info
        try {
            $accountResponse = Http::withHeaders([
                'api-key' => $apiKey,
                'accept'  => 'application/json',
            ])->timeout(15)->get('https://api.brevo.com/v3/account');

            $result['step_1_account_check'] = [
                'status'  => $accountResponse->status(),
                'ok'      => $accountResponse->successful(),
                'body'    => $accountResponse->successful()
                    ? $accountResponse->json()
                    : $accountResponse->body(),
            ];

            if (!$accountResponse->successful()) {
                $result['conclusion'] = 'Brevo API rejected the key (HTTP ' . $accountResponse->status() . '). Regenerate the API key in Brevo dashboard → SMTP & API → API Keys, and update BREVO_API_KEY in Railway.';
                return response()->json($result);
            }
        } catch (\Throwable $e) {
            $result['step_1_account_check'] = [
                'exception' => get_class($e),
                'message'   => $e->getMessage(),
            ];
            $result['conclusion'] = 'Could not reach api.brevo.com — Railway may be blocking outbound HTTPS too, or DNS is broken.';
            return response()->json($result);
        }

        // STEP 2: Actually send a test email
        try {
            $sendResponse = Http::withHeaders([
                'api-key'      => $apiKey,
                'accept'       => 'application/json',
                'content-type' => 'application/json',
            ])->timeout(30)->post('https://api.brevo.com/v3/smtp/email', [
                'sender' => [
                    'email' => $fromAddress,
                    'name'  => $fromName ?? 'Smart Trash Bin',
                ],
                'to' => [['email' => $fromAddress, 'name' => 'Diagnostic']],
                'subject'     => 'Brevo HTTP API diagnostic test',
                'htmlContent' => '<p>Brevo HTTP API is working from Railway.</p><p>Sent at ' . now()->toISOString() . '</p>',
            ]);

            $result['step_2_send_test'] = [
                'status' => $sendResponse->status(),
                'ok'     => $sendResponse->successful(),
                'body'   => $sendResponse->json() ?? $sendResponse->body(),
            ];

            $result['conclusion'] = $sendResponse->successful()
                ? 'Success — Brevo HTTP API works from Railway. Alerts will now send.'
                : 'Brevo rejected the send. Check sender verification: the FROM address must be verified under Brevo → Senders & IP → Senders.';
        } catch (\Throwable $e) {
            $result['step_2_send_test'] = [
                'exception' => get_class($e),
                'message'   => $e->getMessage(),
            ];
            $result['conclusion'] = 'Send failed due to exception.';
        }

        return response()->json($result);
    }

    public function diagnoseMailgun()
    {
        $apiKey   = config('services.mailgun.api_key');
        $domain   = config('services.mailgun.domain');
        $endpoint = config('services.mailgun.endpoint', 'api.mailgun.net');
        $from     = config('mail.from.address');
        $fromName = config('mail.from.name');

        $result = [
            'timestamp' => now()->toISOString(),
            'config' => [
                'mailgun_api_key_set'    => !empty($apiKey),
                'mailgun_api_key_length' => strlen((string) $apiKey),
                'mailgun_api_key_prefix' => substr((string) $apiKey, 0, 8),
                'mailgun_domain'         => $domain,
                'mailgun_endpoint'       => $endpoint,
                'from_address'           => $from,
                'from_name'              => $fromName,
            ],
            'step_1_domain_info' => null,
            'step_2_send_test'   => null,
        ];

        if (empty($apiKey) || empty($domain)) {
            $result['error'] = 'MAILGUN_API_KEY or MAILGUN_DOMAIN not set in Railway variables.';
            return response()->json($result);
        }

        // STEP 1: Verify API key + domain by fetching domain info
        try {
            $domainResp = Http::withBasicAuth('api', $apiKey)
                ->timeout(15)
                ->get("https://{$endpoint}/v4/domains/{$domain}");

            $result['step_1_domain_info'] = [
                'status' => $domainResp->status(),
                'ok'     => $domainResp->successful(),
                'body'   => $domainResp->successful() ? $domainResp->json() : $domainResp->body(),
            ];

            if (!$domainResp->successful()) {
                $result['conclusion'] = 'Mailgun rejected API key or domain (HTTP ' . $domainResp->status() . '). Check MAILGUN_API_KEY and MAILGUN_DOMAIN in Railway. For EU region, set MAILGUN_ENDPOINT=api.eu.mailgun.net.';
                return response()->json($result);
            }
        } catch (\Throwable $e) {
            $result['step_1_domain_info'] = [
                'exception' => get_class($e),
                'message'   => $e->getMessage(),
            ];
            $result['conclusion'] = 'Could not reach api.mailgun.net.';
            return response()->json($result);
        }

        // STEP 2: Send a test email to the FROM address (must be an authorized sandbox recipient)
        try {
            $sendResp = Http::asForm()
                ->withBasicAuth('api', $apiKey)
                ->timeout(30)
                ->post("https://{$endpoint}/v3/{$domain}/messages", [
                    'from'    => $fromName ? "{$fromName} <{$from}>" : $from,
                    'to'      => $from,
                    'subject' => 'Mailgun HTTPS API diagnostic test',
                    'html'    => '<p>Mailgun HTTPS API is working from Railway.</p><p>Sent at ' . now()->toISOString() . '</p>',
                ]);

            $result['step_2_send_test'] = [
                'status' => $sendResp->status(),
                'ok'     => $sendResp->successful(),
                'body'   => $sendResp->json() ?? $sendResp->body(),
            ];

            $result['conclusion'] = $sendResp->successful()
                ? 'Success — Mailgun HTTPS API works from Railway. Now run /api/test/fire-alert?type=gas_leak'
                : 'Mailgun rejected the send. If this is sandbox mode, every recipient email must be authorized in Mailgun → Sending → Domains → ' . $domain . ' → Authorized Recipients.';
        } catch (\Throwable $e) {
            $result['step_2_send_test'] = [
                'exception' => get_class($e),
                'message'   => $e->getMessage(),
            ];
        }

        return response()->json($result);
    }

    public function fireAlert(Request $request)
    {
        $type = (string) $request->query('type', 'gas_leak');
        $deviceUid = (string) $request->query('device', '');

        $validTypes = [
            'gas_leak', 'gas_elevated',
            'trash_full', 'trash_warning',
            'weight_critical', 'weight_warning',
            'battery_health',
        ];

        if (!in_array($type, $validTypes, true)) {
            return response()->json([
                'error'       => "Invalid type '{$type}'.",
                'valid_types' => $validTypes,
                'usage'       => 'GET /api/test/fire-alert?type=gas_leak&device=ESP32_001_bin1',
            ], 400);
        }

        $device = $deviceUid !== ''
            ? Device::where('uid', $deviceUid)->first()
            : Device::where('uid', 'like', 'ESP32_001_%')->first();

        if (!$device) {
            $device = Device::whereNotNull('parent_device_id')->first();
        }
        if (!$device) {
            $device = Device::first();
        }
        if (!$device) {
            return response()->json(['error' => 'No device exists in DB. Send at least one bin-data payload first.'], 404);
        }

        $templates = [
            'gas_leak'        => '🔥 TEST: Flammable gas detected! MQ raw: 850 (threshold: 500).',
            'gas_elevated'   => '⚠️ TEST: Gas level elevated. MQ raw: 420.',
            'trash_full'      => '🚨 TEST: Bin is full. Fill level: 95%.',
            'trash_warning'   => '⚠️ TEST: Bin filling up. Fill level: 65%.',
            'weight_critical' => '⚖️ TEST: Weight critical. Current: 38.0 kg (max: 40 kg).',
            'weight_warning'  => '⚖️ TEST: Weight warning. Current: 22.0 kg.',
            'battery_health'  => '🔋 TEST: Battery dropped to 70%. Fill 45%, Weight 12 kg, Gas normal.',
        ];

        $alert = Alert::create([
            'device_id' => $device->id,
            'type'      => $type,
            'message'   => $templates[$type],
            'status'    => 'active',
        ]);

        $recipients = User::where('notification_enabled', true)->pluck('email')->toArray();

        try {
            event(new AlertCreated($alert));
            $dispatched = true;
            $error = null;
        } catch (\Throwable $e) {
            $dispatched = false;
            $error = [
                'class'   => get_class($e),
                'message' => $e->getMessage(),
            ];
            Log::error('fireAlert: AlertCreated event failed', $error);
        }

        return response()->json([
            'status'            => $dispatched ? 'dispatched' : 'failed',
            'alert'             => [
                'id'      => $alert->id,
                'type'    => $alert->type,
                'message' => $alert->message,
                'device'  => [
                    'id'       => $device->id,
                    'uid'      => $device->uid,
                    'name'     => $device->name,
                    'location' => $device->location,
                ],
            ],
            'recipients_count'  => count($recipients),
            'recipients'        => $recipients,
            'error'             => $error,
            'note'              => 'If dispatched=true and recipients have notification_enabled=true, the Brevo channel was called. Check inbox and spam. Check Brevo → Statistics for delivery status.',
        ]);
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

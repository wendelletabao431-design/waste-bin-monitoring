<?php

namespace App\Notifications;

use Illuminate\Bus\Queueable;
use Illuminate\Notifications\Notification;
use App\Models\Alert;
use App\Notifications\Channels\MailgunChannel;

class AlertNotification extends Notification
{
    use Queueable;

    public function __construct(public Alert $alert) {}

    public function via(object $notifiable): array
    {
        return [MailgunChannel::class];
    }

    public function toMailgun(object $notifiable): array
    {
        return $this->buildPayload();
    }

    public function toBrevo(object $notifiable): array
    {
        return $this->buildPayload();
    }

    private function buildPayload(): array
    {
        $severity = match($this->alert->type) {
            'gas_leak'       => 'CRITICAL',
            'trash_full'     => 'CRITICAL',
            'gas_elevated'   => 'WARNING',
            'trash_warning'  => 'WARNING',
            'battery_health' => 'HEALTH REPORT',
            default          => 'INFO',
        };

        $emoji = match($this->alert->type) {
            'gas_leak'       => '🔴',
            'trash_full'     => '🔴',
            'gas_elevated'   => '🟡',
            'trash_warning'  => '🟡',
            'battery_health' => '🔋',
            default          => 'ℹ️',
        };

        $frontendUrl = config('app.frontend_url', 'https://smart-trash-bin-mu.vercel.app');

        $device   = $this->alert->device;
        $name     = e($device->name ?? 'Unknown device');
        $location = e($device->location ?? 'Unknown location');
        $type     = e($this->alert->type);
        $message  = e($this->alert->message);
        $time     = e($this->alert->created_at->format('M d, Y H:i:s'));

        $subject = "{$emoji} {$severity}: {$name} Alert";

        $html = <<<HTML
<!DOCTYPE html>
<html>
<head><meta charset="UTF-8"></head>
<body style="font-family:Arial,sans-serif;background:#f5f5f5;padding:24px;margin:0;">
  <table role="presentation" cellspacing="0" cellpadding="0" border="0" width="600" align="center" style="background:#ffffff;border-radius:8px;overflow:hidden;box-shadow:0 1px 3px rgba(0,0,0,0.08);">
    <tr>
      <td style="padding:24px 32px;border-bottom:1px solid #eee;">
        <h2 style="margin:0;color:#222;">Smart Trash Bin Alert</h2>
        <p style="margin:4px 0 0;color:#888;font-size:14px;">{$emoji} {$severity}</p>
      </td>
    </tr>
    <tr>
      <td style="padding:24px 32px;color:#333;font-size:15px;line-height:1.6;">
        <p><strong>Device:</strong> {$name}</p>
        <p><strong>Location:</strong> {$location}</p>
        <p><strong>Alert Type:</strong> {$type}</p>
        <p><strong>Message:</strong> {$message}</p>
        <p><strong>Time:</strong> {$time}</p>
        <p style="margin-top:28px;">
          <a href="{$frontendUrl}" style="display:inline-block;padding:10px 20px;background:#2563eb;color:#fff;text-decoration:none;border-radius:6px;">View Dashboard</a>
        </p>
        <p style="margin-top:24px;color:#555;">Please take appropriate action.</p>
      </td>
    </tr>
  </table>
</body>
</html>
HTML;

        return [
            'subject' => $subject,
            'html'    => $html,
        ];
    }
}

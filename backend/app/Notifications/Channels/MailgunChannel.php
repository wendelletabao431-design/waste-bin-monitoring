<?php

declare(strict_types=1);

namespace App\Notifications\Channels;

use Illuminate\Notifications\Notification;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\Log;

class MailgunChannel
{
    public function send(object $notifiable, Notification $notification): void
    {
        if (!method_exists($notification, 'toMailgun')) {
            return;
        }

        $toEmail = method_exists($notifiable, 'routeNotificationFor')
            ? $notifiable->routeNotificationFor('mail', $notification)
            : ($notifiable->email ?? null);

        if (empty($toEmail)) {
            Log::warning('MailgunChannel: no recipient email on notifiable', [
                'notifiable' => get_class($notifiable),
            ]);
            return;
        }

        $payload = $notification->toMailgun($notifiable);

        $apiKey   = config('services.mailgun.api_key');
        $domain   = config('services.mailgun.domain');
        $endpoint = config('services.mailgun.endpoint', 'api.mailgun.net');

        if (empty($apiKey) || empty($domain)) {
            Log::error('MailgunChannel: MAILGUN_API_KEY or MAILGUN_DOMAIN is not configured');
            throw new \RuntimeException('Mailgun is not configured.');
        }

        $fromEmail = config('mail.from.address');
        $fromName  = config('mail.from.name', 'Smart Trash Bin');
        $toName    = (string) ($notifiable->name ?? '');
        $to        = is_array($toEmail) ? reset($toEmail) : $toEmail;

        $response = Http::asForm()
            ->withBasicAuth('api', $apiKey)
            ->timeout(30)
            ->post("https://{$endpoint}/v3/{$domain}/messages", [
                'from'    => $fromName !== '' ? "{$fromName} <{$fromEmail}>" : $fromEmail,
                'to'      => $toName !== '' ? "{$toName} <{$to}>" : $to,
                'subject' => $payload['subject'],
                'html'    => $payload['html'],
            ]);

        if ($response->failed()) {
            Log::error('MailgunChannel: API send failed', [
                'status' => $response->status(),
                'body'   => $response->body(),
            ]);
            throw new \RuntimeException(
                'Mailgun API send failed (' . $response->status() . '): ' . $response->body()
            );
        }
    }
}

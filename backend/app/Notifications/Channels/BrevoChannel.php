<?php

declare(strict_types=1);

namespace App\Notifications\Channels;

use Illuminate\Notifications\Notification;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\Log;

class BrevoChannel
{
    public function send(object $notifiable, Notification $notification): void
    {
        if (!method_exists($notification, 'toBrevo')) {
            return;
        }

        $toEmail = method_exists($notifiable, 'routeNotificationFor')
            ? $notifiable->routeNotificationFor('mail', $notification)
            : ($notifiable->email ?? null);

        if (empty($toEmail)) {
            Log::warning('BrevoChannel: no recipient email on notifiable', [
                'notifiable' => get_class($notifiable),
            ]);
            return;
        }

        $payload = $notification->toBrevo($notifiable);

        $apiKey = config('services.brevo.api_key');
        if (empty($apiKey)) {
            Log::error('BrevoChannel: BREVO_API_KEY is not set');
            throw new \RuntimeException('BREVO_API_KEY is not configured.');
        }

        $response = Http::withHeaders([
            'api-key'      => $apiKey,
            'accept'       => 'application/json',
            'content-type' => 'application/json',
        ])->timeout(30)->post('https://api.brevo.com/v3/smtp/email', [
            'sender' => [
                'email' => config('mail.from.address'),
                'name'  => config('mail.from.name', 'Smart Trash Bin'),
            ],
            'to' => [[
                'email' => is_array($toEmail) ? reset($toEmail) : $toEmail,
                'name'  => (string) ($notifiable->name ?? ''),
            ]],
            'subject'     => $payload['subject'],
            'htmlContent' => $payload['html'],
        ]);

        if ($response->failed()) {
            Log::error('BrevoChannel: API send failed', [
                'status' => $response->status(),
                'body'   => $response->body(),
            ]);
            throw new \RuntimeException(
                'Brevo API send failed (' . $response->status() . '): ' . $response->body()
            );
        }
    }
}

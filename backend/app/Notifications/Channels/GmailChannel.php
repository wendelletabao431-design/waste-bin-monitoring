<?php

declare(strict_types=1);

namespace App\Notifications\Channels;

use Illuminate\Notifications\Notification;
use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\Log;

class GmailChannel
{
    public function send(object $notifiable, Notification $notification): void
    {
        if (!method_exists($notification, 'toGmail')) {
            return;
        }

        foreach (['client_id', 'client_secret', 'refresh_token', 'from'] as $key) {
            if (empty(config("services.gmail.{$key}"))) {
                Log::error("GmailChannel: missing config key services.gmail.{$key}");
                throw new \RuntimeException("Gmail OAuth2: missing config key 'services.gmail.{$key}'. Add GMAIL_" . strtoupper($key) . " to your .env file.");
            }
        }

        $toEmail = method_exists($notifiable, 'routeNotificationFor')
            ? $notifiable->routeNotificationFor('mail', $notification)
            : ($notifiable->email ?? null);

        if (empty($toEmail)) {
            Log::warning('GmailChannel: no recipient email on notifiable', [
                'notifiable' => get_class($notifiable),
            ]);
            return;
        }

        $payload     = $notification->toGmail($notifiable);
        $accessToken = $this->getAccessToken();

        if (!$accessToken) {
            Log::error('GmailChannel: failed to obtain access token');
            throw new \RuntimeException('Gmail OAuth2: could not retrieve access token.');
        }

        $raw      = $this->buildRawMessage($toEmail, $payload['subject'], $payload['html']);
        $response = Http::withToken($accessToken)
            ->timeout(30)
            ->withOptions(['verify' => config('services.gmail.verify_ssl', true)])
            ->post('https://gmail.googleapis.com/gmail/v1/users/me/messages/send', [
                'raw' => $raw,
            ]);

        if ($response->failed()) {
            Log::error('GmailChannel: send failed', [
                'status' => $response->status(),
                'body'   => $response->body(),
            ]);
            throw new \RuntimeException(
                'Gmail API send failed (' . $response->status() . '): ' . $response->body()
            );
        }

        Log::info('GmailChannel: email sent', ['to' => $toEmail]);
    }

    private function getAccessToken(): ?string
    {
        // Cache the token for 50 min — Gmail access tokens expire after 60 min
        return Cache::remember('gmail_access_token', 3000, function () {
            $response = Http::timeout(15)
                ->withOptions(['verify' => config('services.gmail.verify_ssl', true)])
                ->post('https://oauth2.googleapis.com/token', [
                'client_id'     => config('services.gmail.client_id'),
                'client_secret' => config('services.gmail.client_secret'),
                'refresh_token' => config('services.gmail.refresh_token'),
                'grant_type'    => 'refresh_token',
            ]);

            if ($response->failed()) {
                Log::error('GmailChannel: token refresh failed', [
                    'status' => $response->status(),
                    'body'   => $response->body(),
                ]);
                return null;
            }

            return $response->json('access_token');
        });
    }

    private function buildRawMessage(string $to, string $subject, string $html): string
    {
        $from = config('services.gmail.from');

        $message = implode("\r\n", [
            "From: {$from}",
            "To: {$to}",
            "Subject: {$subject}",
            'MIME-Version: 1.0',
            'Content-Type: text/html; charset=UTF-8',
            '',
            $html,
        ]);

        // Gmail API requires base64url encoding (not standard base64)
        return rtrim(strtr(base64_encode($message), '+/', '-_'), '=');
    }
}

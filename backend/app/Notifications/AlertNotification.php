<?php

namespace App\Notifications;

use Illuminate\Bus\Queueable;
use Illuminate\Notifications\Notification;
use Illuminate\Notifications\Messages\MailMessage;
use App\Models\Alert;

class AlertNotification extends Notification
{
    use Queueable;

    public function __construct(public Alert $alert) {}

    public function via(object $notifiable): array
    {
        return ['mail'];
    }

    public function toMail(object $notifiable): MailMessage
    {
        $severity = match($this->alert->type) {
            'gas_leak' => 'CRITICAL',
            'trash_full' => 'CRITICAL',
            'gas_elevated' => 'WARNING',
            'trash_warning' => 'WARNING',
            default => 'INFO',
        };

        $emoji = match($this->alert->type) {
            'gas_leak' => '🔴',
            'trash_full' => '🔴',
            'gas_elevated' => '🟡',
            'trash_warning' => '🟡',
            default => 'ℹ️',
        };

        $frontendUrl = config('app.frontend_url', 'https://smart-trash-bin-mu.vercel.app');

        return (new MailMessage)
            ->subject("{$emoji} {$severity}: {$this->alert->device->name} Alert")
            ->greeting("Smart Trash Bin Alert")
            ->line("**Device:** {$this->alert->device->name}")
            ->line("**Location:** {$this->alert->device->location}")
            ->line("**Alert Type:** {$this->alert->type}")
            ->line("**Message:** {$this->alert->message}")
            ->line("**Time:** {$this->alert->created_at->format('M d, Y H:i:s')}")
            ->action('View Dashboard', $frontendUrl)
            ->line('Please take appropriate action.');
    }
}

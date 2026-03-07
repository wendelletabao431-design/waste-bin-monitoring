<?php

namespace App\Listeners;

use App\Events\AlertCreated;
use App\Models\User;
use App\Notifications\AlertNotification;
use Illuminate\Support\Facades\Log;

class SendAlertNotification
{
    public function handle(AlertCreated $event): void
    {
        $alert = $event->alert;

        $users = User::where('notification_enabled', true)->get();

        Log::info("Sending alert notification", [
            'alert_id' => $alert->id,
            'alert_type' => $alert->type,
            'recipients' => $users->count(),
        ]);

        foreach ($users as $user) {
            $user->notify(new AlertNotification($alert));
        }
    }
}

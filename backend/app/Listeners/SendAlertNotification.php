<?php

namespace App\Listeners;

use App\Events\AlertCreated;
use App\Models\Alert;
use App\Models\User;
use App\Notifications\AlertNotification;
use Illuminate\Support\Facades\Log;

class SendAlertNotification
{
    public function handle(AlertCreated $event): void
    {
        $alertId = $event->alert->id;

        dispatch(function () use ($alertId): void {
            $alert = Alert::with('device')->find($alertId);

            if (!$alert) {
                Log::warning("Alert notification skipped because alert no longer exists", [
                    'alert_id' => $alertId,
                ]);
                return;
            }

            $users = User::where('notification_enabled', true)->get();

            Log::info("Sending alert notification", [
                'alert_id' => $alert->id,
                'alert_type' => $alert->type,
                'recipients' => $users->count(),
            ]);

            foreach ($users as $user) {
                try {
                    $user->notify(new AlertNotification($alert));
                } catch (\Throwable $e) {
                    Log::error("Alert notification failed", [
                        'alert_id' => $alert->id,
                        'user_id' => $user->id,
                        'message' => $e->getMessage(),
                    ]);
                }
            }
        })->afterResponse();
    }
}

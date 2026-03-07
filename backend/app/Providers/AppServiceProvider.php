<?php

namespace App\Providers;

use App\Models\Device;
use Illuminate\Support\ServiceProvider;
use Illuminate\Support\Facades\Event;
use Illuminate\Support\Facades\Schema;
use Illuminate\Support\Facades\URL;
use App\Events\AlertCreated;
use App\Listeners\SendAlertNotification;
use Database\Seeders\DatabaseSeeder;

class AppServiceProvider extends ServiceProvider
{
    public function register(): void
    {
        //
    }

    public function boot(): void
    {
        if (app()->environment('production')) {
            URL::forceScheme('https');
        }

        if (
            app()->environment('local') &&
            !app()->runningInConsole() &&
            $this->hasDemoDataTables() &&
            !Device::query()->exists()
        ) {
            app(DatabaseSeeder::class)->run();
        }

        Event::listen(
            AlertCreated::class,
            SendAlertNotification::class
        );
    }

    private function hasDemoDataTables(): bool
    {
        return Schema::hasTable('users')
            && Schema::hasTable('devices')
            && Schema::hasTable('sensor_readings')
            && Schema::hasTable('collections');
    }
}

# Smart Trash Bin IoT System - Laravel Backend

## Overview
Laravel API backend for the Smart Trash Bin monitoring system. Receives sensor data from ESP32 hardware and provides dashboard data to the React frontend.

## Requirements
- PHP 8.1+
- MySQL 8.0+
- Composer

## Local Development
```bash
composer install
cp .env.example .env
php artisan key:generate
php artisan migrate --seed
php artisan serve
```

If your local `devices` table is empty, the backend will also restore the demo bins automatically on the first browser/API request in `APP_ENV=local`. This keeps the dashboard map available without affecting production.

## Production Deployment (Railway.app)

### Environment Variables Required
```
APP_NAME="Smart Trash Bin"
APP_ENV=production
APP_DEBUG=false
APP_KEY=base64:... (generate with php artisan key:generate --show)
APP_URL=https://your-app.up.railway.app

DB_CONNECTION=mysql
DB_HOST=${{MySQL.MYSQLHOST}}
DB_PORT=${{MySQL.MYSQLPORT}}
DB_DATABASE=${{MySQL.MYSQLDATABASE}}
DB_USERNAME=${{MySQL.MYSQLUSER}}
DB_PASSWORD=${{MySQL.MYSQLPASSWORD}}

SESSION_DRIVER=cookie
CACHE_STORE=file
```

## API Endpoints

### ESP32 Hardware Endpoint
```
POST /api/bin-data
Content-Type: application/json

{
  "device_id": "AA:BB:CC:DD:EE:FF",
  "battery_voltage": 11.82,
  "bin_1": { "distance_cm": 59.5, "hx711_raw": 50000, "mq_raw": 200 },
  "bin_2": { "distance_cm": 60.0, "hx711_raw": 30000, "mq_raw": 150 }
}
```

### Dashboard Endpoints
- `GET /api/dashboard/summary` - System overview
- `GET /api/devices` - All bins with latest readings
- `GET /api/devices/{id}/details` - Bin efficiency, collections
- `GET /api/devices/{id}/history` - Historical sensor data

### Auth Endpoints
- `POST /api/register` - User registration
- `POST /api/login` - User login
- `POST /api/logout` - User logout (auth required)

## Calibration
Sensor calibration constants are in `config/sensors.php`. These must match ESP32 firmware values.

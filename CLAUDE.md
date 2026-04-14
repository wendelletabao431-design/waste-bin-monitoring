# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Smart Trash Bin IoT monitoring system with three layers:
- **ESP32 firmware** (`esp32_firmware_fixed.ino`) — reads sensors, sends raw data to backend
- **Laravel backend** (`backend/`) — derives all metrics server-side, stores readings, fires alerts
- **React frontend** (`trash-bin-butler/`) — dashboard UI deployed on Vercel

## Commands

### Backend (run from `backend/`)
```bash
composer install
cp .env.example .env && php artisan key:generate
php artisan migrate --seed
php artisan serve                    # Dev server on :8000
php artisan test                     # Run PHPUnit tests
vendor/bin/phpunit --coverage-text   # With coverage
```

### Frontend (run from `trash-bin-butler/`)
```bash
npm install
npm run dev       # Vite dev server
npm run build     # tsc -b && vite build
npm run lint      # ESLint
```

## Environment Variables

### Backend `.env`
```
APP_ENV=local
DB_CONNECTION=mysql
DB_HOST=127.0.0.1
DB_DATABASE=...
DB_USERNAME=...
DB_PASSWORD=...
GOOGLE_CLIENT_ID=...   # For Google OAuth login
```

### Frontend `.env` (in `trash-bin-butler/`)
```
VITE_API_URL=http://localhost:8000/api
VITE_GOOGLE_CLIENT_ID=...
```

## Architecture

### Data Flow

```
ESP32 → POST /api/bin-data (raw values) → EspController → derives metrics → SensorReadings table
                                                                           → Alert events
Dashboard UI → GET /api/devices, /api/dashboard/summary → DashboardController (public, no auth)
```

**Critical design rule:** The ESP32 sends only raw sensor values (`distance_cm`, `hx711_raw`, `mq_raw`, `battery_voltage`). All derivation (fill %, weight kg, gas level, battery %) happens exclusively in `EspController`. Never move derivation to firmware — calibration changes must only require a backend deploy.

### Device Identity

Each physical ESP32 manages two bins. Identity resolution in `EspController::resolveBinDevice()`:
- `parent_device_id` = the ESP32's `DEVICE_ID` constant (e.g. `"ESP32_001"`)
- `bin_number` = 1 or 2
- `uid` = `"{parent_device_id}_bin{binNumber}"` (e.g. `"ESP32_001_bin1"`)

**Demo placeholders**: Devices with UIDs `DEMO_BIN_001`/`DEMO_BIN_002` and `parent_device_id = NULL` are seeded on first run. `filterVisibleDevices()` in `DashboardController` hides a demo placeholder once a real device exists for that bin slot.

### Sensor Calibration

Calibration constants live in `backend/config/sensors.php`.

Ultrasonic calibration:
- Bin 1: empty = 58.0 cm, full = 10.0 cm (50% at 34 cm, 90% at 14.8 cm)
- Bin 2: empty = 48.0 cm, full = 10.0 cm (50% at 29 cm, 90% at 13.8 cm)

Weight calibration (scale factors in `EspController::deriveWeight()`, read from config):
- Bin 1: 119,800 raw/kg, max 40 kg (warning at 20 kg, critical at 36 kg)
- Bin 2: 117,786 raw/kg, max 20 kg (warning at 10 kg, critical at 18 kg)

Battery: firmware sends `battery_voltage` pre-computed via `adc * 4.0` (pin 33, voltage divider 4.0×). The `voltage_divider` config key reflects this. The `battery_adc` fallback path in `EspController` is legacy-only.

### Authentication

- **Frontend ↔ Backend**: Laravel Sanctum Bearer tokens, stored in `localStorage`. Token sent as `Authorization: Bearer <token>`.
- **Auth routes** (`/register`, `/login`, `/auth/google`) are public.
- **Dashboard/device routes** (`/dashboard/summary`, `/devices/*`) are **unauthenticated** — no Sanctum middleware.
- **User routes** (`/user`, `/logout`, profile/password endpoints) require `auth:sanctum`.
- Google OAuth is validated by calling `https://oauth2.googleapis.com/tokeninfo` directly (no Socialite).

### Alert System

Alerts fire inside `EspController` during `processBinData()`:
- `trash_warning`: fill ≥ 50% — auto-resolves at < 40%
- `trash_full` (critical): fill ≥ 90%
- `gas_leak`: MQ raw ≥ 500
- `weight_warning`: weight ≥ per-bin warning threshold (Bin1=20 kg, Bin2=10 kg)
- `weight_critical`: weight ≥ per-bin critical threshold (Bin1=36 kg, Bin2=18 kg)

`DashboardController::summary()` counts `weight_critical` as critical and `weight_warning` as warning alongside fill/gas types.

`AlertCreated` event → `SendAlertNotification` listener → `AlertNotification` (database notification on users with `notification_enabled = true`).

Collection detection: fill drops from ≥ 90% → ≤ 20% creates a `Collection` record and resolves active `trash_full` alerts.

### Battery Handling

Two paths for battery data:
1. **Full payload**: `battery_percent` is derived and stored both in `sensor_readings` (type=battery) and in the `devices.battery_percent` column.
2. **Battery-only payload** (no `bin_1`/`bin_2`): firmware's `sendBatteryOnly` sends only `device_id` + `battery_voltage`, which updates `devices.battery_percent` without creating bin readings.

`DashboardController` prefers the cached `devices.battery_percent` column, falling back to the readings table.

### Firmware Modes

`esp32_firmware_fixed.ino` operates in two modes detected by INA219 current > 0.1 A:
- **Solar mode**: all hardware on, LEDs/LCD active, sends every 60 s
- **Idle mode**: all LEDs and LCD off, gas sensor only, sends every 10 min or on gas alert

Gas alert in either mode: 5 s hardware stabilization → read all → send `gas_detected`; poll until clear → send `gas_normal`. In idle mode no LEDs or LCD activate during the alert.

LED pins: 26 = Green (power, solid in solar mode), 4 = Yellow (WiFi: blink while connecting, solid when connected), 2 = Blue (solid on in solar mode, goes LOW during HTTP POST as send indicator).

### Frontend Structure

`trash-bin-butler/src/`:
- `context/AuthContext.tsx` — auth state, login/logout, wraps the app
- `services/api.ts` — all HTTP calls, reads Bearer token from `localStorage`
- `types/bin.ts` — `Bin`, `BinSummary`, `Status` types
- `components/Dashboard.tsx` — main view, polling
- `pages/AuthPage.tsx` — login/register/Google OAuth
- `utils/binStatus.ts` — fill-level color/label functions (thresholds: Normal ≤49%, Warning ≤89%, Critical ≥90%)

Routes: `/login` → `AuthPage`, `/` → `Dashboard`, `/settings` → `ProfileSettings`

## Known Issues

- **Test endpoints exist**: `GET /api/test/alerts` and `GET /api/test/cleanup` in `TestController` are still registered and public — remove before production.
- **Dashboard endpoints are unauthenticated**: `/api/devices` and `/api/dashboard/summary` have no auth middleware.
- **Firmware has hardcoded WiFi credentials** in `esp32_firmware_fixed.ino` — do not commit updated credentials to a public repo.

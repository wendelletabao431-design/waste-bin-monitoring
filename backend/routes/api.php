<?php

use Illuminate\Http\Request;
use Illuminate\Support\Facades\Route;
use App\Http\Controllers\EspController;
use App\Http\Controllers\DashboardController;
use App\Http\Controllers\AuthController;
use App\Http\Controllers\TestController;

// Public Auth Routes
Route::post('/register', [AuthController::class, 'register']);
Route::post('/login', [AuthController::class, 'login']);
Route::post('/auth/google', [AuthController::class, 'googleLogin']);

// Protect User Routes with Sanctum
Route::middleware('auth:sanctum')->group(function () {
    Route::post('/logout', [AuthController::class, 'logout']);
    Route::get('/user', [AuthController::class, 'user']);
    Route::put('/user/profile', [AuthController::class, 'updateProfile']);
    Route::put('/user/password', [AuthController::class, 'updatePassword']);
    Route::put('/user/notifications', [AuthController::class, 'updateNotifications']);
    Route::delete('/user/account', [AuthController::class, 'deleteAccount']);
});

// ESP32 Endpoints
Route::prefix('esp')->group(function () {
    Route::post('/readings', [EspController::class, 'storeReadings']); // Legacy
    Route::get('/health', [EspController::class, 'health']); // Health check
});

// Primary ESP32 Endpoint (Raw Sensor Data)
// This is the main endpoint the ESP32 firmware should target
Route::post('/bin-data', [EspController::class, 'receiveBinData']);

// Dashboard Endpoints
Route::prefix('dashboard')->group(function () {
    Route::get('/summary', [DashboardController::class, 'summary']);
});

// Device Endpoints
Route::get('/devices', [DashboardController::class, 'devices']);
Route::get('/devices/grouped', [DashboardController::class, 'devicesGrouped']);
Route::get('/devices/{id}/details', [DashboardController::class, 'deviceDetails']);
Route::get('/devices/{id}/history', [DashboardController::class, 'deviceHistory']);

// Test Endpoints (REMOVE AFTER TESTING)
Route::get('/test/alerts', [TestController::class, 'testAlerts']);
Route::get('/test/cleanup', [TestController::class, 'cleanup']);
Route::get('/test/smtp-diagnose', [TestController::class, 'diagnoseSmtp']);
Route::get('/test/brevo-diagnose', [TestController::class, 'diagnoseBrevoApi']);
Route::get('/test/mailgun-diagnose', [TestController::class, 'diagnoseMailgun']);
Route::get('/test/fire-alert', [TestController::class, 'fireAlert']);
Route::get('/test/gmail-diagnose', [TestController::class, 'diagnoseGmail']);

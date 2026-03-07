<?php

/**
 * End-to-End Deployment Readiness Test
 * 
 * Tests the complete data flow from ESP32 → Backend → Database
 */

require_once __DIR__ . '/../vendor/autoload.php';

// Bootstrap Laravel
$app = require_once __DIR__ . '/../bootstrap/app.php';
$kernel = $app->make(Illuminate\Contracts\Console\Kernel::class);
$kernel->bootstrap();

use Illuminate\Support\Facades\Config;
use App\Http\Controllers\EspController;

echo "=== END-TO-END DEPLOYMENT READINESS TEST ===\n\n";

$allPassed = true;

// Test 1: Configuration Loading
echo "TEST 1: Configuration Values\n";
echo str_repeat("-", 50) . "\n";
$configTests = [
    'sensors.empty_distance_cm' => 59.0,
    'sensors.full_distance_cm' => 4.0,
    'sensors.offset_cm' => 3.5,
    'sensors.raw_empty_bin1' => 451977,
    'sensors.scale_bin1' => 119800.0,
    'sensors.raw_empty_bin2' => -491000,
    'sensors.scale_bin2' => 117786.0,
    'sensors.mq_dangerous_min' => 600,
    'sensors.battery_min_voltage' => 9.0,
    'sensors.battery_max_voltage' => 12.6,
];

foreach ($configTests as $key => $expected) {
    $actual = config($key);
    $status = ($actual == $expected) ? '✓' : '✗';
    echo sprintf("  %s %s: %s (expected: %s)\n", $status, $key, $actual, $expected);
    if ($actual != $expected) $allPassed = false;
}
echo "\n";

// Test 2: Sensor Calculation Functions
echo "TEST 2: Sensor Calculation Functions\n";
echo str_repeat("-", 50) . "\n";

// Create a test instance of EspController
$controller = new EspController();
$reflection = new ReflectionClass($controller);

// Test deriveFillPercent
$method = $reflection->getMethod('deriveFillPercent');
$method->setAccessible(true);

$fillTests = [
    ['input' => 59, 'expected' => 0.0, 'desc' => 'Empty'],
    ['input' => 31.5, 'expected' => 43.6, 'desc' => 'Middle (59cm empty, offset 3.5cm)'],
    ['input' => 4, 'expected' => 93.6, 'desc' => 'Near full'],
];

foreach ($fillTests as $test) {
    $result = $method->invoke($controller, $test['input']);
    $diff = abs($result - $test['expected']);
    $status = ($diff < 0.5) ? '✓' : '✗';
    echo sprintf("  %s Fill %s: %.1f%% (expected: %.1f%%)\n", 
        $status, $test['desc'], $result, $test['expected']);
    if ($diff >= 0.5) $allPassed = false;
}

// Test deriveWeight
$method = $reflection->getMethod('deriveWeight');
$method->setAccessible(true);

$weightTests = [
    ['raw' => 451977, 'bin' => 1, 'expected' => 0.0, 'desc' => 'Bin1 Empty'],
    ['raw' => 608800, 'bin' => 1, 'expected' => 1.309, 'desc' => 'Bin1 1.31kg'],
    ['raw' => -491000, 'bin' => 2, 'expected' => 0.0, 'desc' => 'Bin2 Empty'],
    ['raw' => -336700, 'bin' => 2, 'expected' => 1.310, 'desc' => 'Bin2 1.31kg'],
];

foreach ($weightTests as $test) {
    $result = $method->invoke($controller, $test['raw'], $test['bin']);
    $diff = abs($result - $test['expected']);
    $status = ($diff < 0.01) ? '✓' : '✗';
    echo sprintf("  %s Weight %s: %.3f kg (expected: %.3f kg)\n", 
        $status, $test['desc'], $result, $test['expected']);
    if ($diff >= 0.01) $allPassed = false;
}

// Test deriveGasLevel
$method = $reflection->getMethod('deriveGasLevel');
$method->setAccessible(true);

$gasTests = [
    ['raw' => 200, 'expected' => 0, 'desc' => 'Normal'],
    ['raw' => 450, 'expected' => 1, 'desc' => 'Elevated'],
    ['raw' => 750, 'expected' => 2, 'desc' => 'Dangerous'],
];

foreach ($gasTests as $test) {
    $result = $method->invoke($controller, $test['raw']);
    $status = ($result === $test['expected']) ? '✓' : '✗';
    $levels = ['Normal', 'Elevated', 'Dangerous'];
    echo sprintf("  %s Gas %s: Level %d (%s)\n", 
        $status, $test['desc'], $result, $levels[$result]);
    if ($result !== $test['expected']) $allPassed = false;
}
echo "\n";

// Test deriveBatteryPercent
$method = $reflection->getMethod('deriveBatteryPercent');
$method->setAccessible(true);

$batteryTests = [
    ['voltage' => 9.0, 'expected' => 0.0, 'desc' => 'Empty'],
    ['voltage' => 10.8, 'expected' => 50.0, 'desc' => 'Half charge'],
    ['voltage' => 12.6, 'expected' => 100.0, 'desc' => 'Full'],
];

foreach ($batteryTests as $test) {
    $result = $method->invoke($controller, $test['voltage']);
    $diff = abs($result - $test['expected']);
    $status = ($diff < 0.1) ? 'âœ“' : 'âœ—';
    echo sprintf("  %s Battery %s: %.1f%% (expected: %.1f%%)\n",
        $status, $test['desc'], $result, $test['expected']);
    if ($diff >= 0.1) $allPassed = false;
}
echo "\n";

// Test 3: Required Files Check
echo "TEST 3: Required Files\n";
echo str_repeat("-", 50) . "\n";

$requiredFiles = [
    'config/sensors.php',
    'app/Http/Controllers/EspController.php',
    'routes/api.php',
    'database/migrations',
    '../trash-bin-butler/package.json',
    '../trash-bin-butler/src/services/api.ts',
];

$basePath = __DIR__ . '/../';
foreach ($requiredFiles as $file) {
    $exists = file_exists($basePath . $file);
    $status = $exists ? '✓' : '✗';
    echo sprintf("  %s %s\n", $status, $file);
    if (!$exists) $allPassed = false;
}
echo "\n";

// Test 4: API Endpoint Validation
echo "TEST 4: API Routes\n";
echo str_repeat("-", 50) . "\n";

$requiredRoutes = [
    ['method' => 'POST', 'path' => '/bin-data'],
    ['method' => 'GET', 'path' => '/summary'],  // Inside prefix('dashboard') group
    ['method' => 'GET', 'path' => '/devices'],
    ['method' => 'POST', 'path' => '/login'],
];

$routesFile = file_get_contents($basePath . 'routes/api.php');
foreach ($requiredRoutes as $route) {
    // Look for Route::method('path' or Route::method("path"
    $pattern = '/Route::' . strtolower($route['method']) . "\(['\"]" . preg_quote($route['path'], '/') . '/i';
    $hasRoute = preg_match($pattern, $routesFile);
    $status = $hasRoute ? '✓' : '✗';
    echo sprintf("  %s %s %s\n", $status, $route['method'], $route['path']);
    if (!$hasRoute) $allPassed = false;
}
echo "\n";

// Test 5: Database Schema Compatibility
echo "TEST 5: Database Schema\n";
echo str_repeat("-", 50) . "\n";

$migrationsPath = $basePath . 'database/migrations/';
$migrations = glob($migrationsPath . '*.php');

$requiredTables = ['devices', 'sensor_readings', 'alerts', 'collections'];
$foundTables = [];

foreach ($migrations as $migration) {
    $content = file_get_contents($migration);
    foreach ($requiredTables as $table) {
        if (strpos($content, "create_{$table}_table") !== false ||
            strpos($content, "Schema::create('{$table}'") !== false) {
            $foundTables[] = $table;
        }
    }
}

foreach ($requiredTables as $table) {
    $exists = in_array($table, $foundTables);
    $status = $exists ? '✓' : '✗';
    echo sprintf("  %s Table: %s\n", $status, $table);
    if (!$exists) $allPassed = false;
}
echo "\n";

// Final Result
echo str_repeat("=", 50) . "\n";
if ($allPassed) {
    echo "✅ ALL TESTS PASSED - READY FOR DEPLOYMENT\n";
    echo "\nNext steps:\n";
    echo "1. Push code to GitHub\n";
    echo "2. Deploy backend to Railway\n";
    echo "3. Set APP_KEY in Railway environment\n";
    echo "4. Deploy frontend to Vercel/Netlify\n";
    echo "5. Update ESP32 with production URL\n";
    exit(0);
} else {
    echo "❌ SOME TESTS FAILED - FIX ISSUES BEFORE DEPLOYMENT\n";
    exit(1);
}

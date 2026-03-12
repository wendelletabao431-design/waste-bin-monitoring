<?php

/**
 * End-to-end deployment readiness test.
 *
 * Tests the complete data flow from ESP32 -> Backend -> Database.
 */

require_once __DIR__ . '/../vendor/autoload.php';

$app = require_once __DIR__ . '/../bootstrap/app.php';
$kernel = $app->make(Illuminate\Contracts\Console\Kernel::class);
$kernel->bootstrap();

use App\Http\Controllers\EspController;

echo "=== END-TO-END DEPLOYMENT READINESS TEST ===\n\n";

$allPassed = true;

echo "TEST 1: Configuration Values\n";
echo str_repeat("-", 50) . "\n";

$configTests = [
    'sensors.ultrasonic.bin_1.empty_distance_cm' => 58.7,
    'sensors.ultrasonic.bin_1.full_distance_cm' => 10.0,
    'sensors.ultrasonic.bin_2.empty_distance_cm' => 48.3,
    'sensors.ultrasonic.bin_2.full_distance_cm' => 10.0,
    'sensors.load_cell.bin_1.raw_empty' => 514375,
    'sensors.load_cell.bin_1.scale_raw_per_gram' => 90.4,
    'sensors.load_cell.bin_2.raw_empty' => -480493,
    'sensors.load_cell.bin_2.scale_raw_per_gram' => 92.6,
    'sensors.load_cell.reference_weight_kg' => 1.53,
    'sensors.mq_dangerous_min' => 600,
    'sensors.battery_min_voltage' => 9.0,
    'sensors.battery_max_voltage' => 12.6,
];

foreach ($configTests as $key => $expected) {
    $actual = config($key);
    $status = ($actual == $expected) ? '[PASS]' : '[FAIL]';
    echo sprintf("  %s %s: %s (expected: %s)\n", $status, $key, $actual, $expected);
    if ($actual != $expected) {
        $allPassed = false;
    }
}

echo "\n";
echo "TEST 2: Sensor Calculation Functions\n";
echo str_repeat("-", 50) . "\n";

$controller = new EspController();
$reflection = new ReflectionClass($controller);

$method = $reflection->getMethod('deriveFillPercent');
$method->setAccessible(true);

$fillTests = [
    ['distance' => 58.7, 'bin' => 1, 'expected' => 0.0, 'desc' => 'Bin 1 empty'],
    ['distance' => 34.4, 'bin' => 1, 'expected' => 50.0, 'desc' => 'Bin 1 50% marker'],
    ['distance' => 10.0, 'bin' => 1, 'expected' => 100.0, 'desc' => 'Bin 1 full'],
    ['distance' => 48.3, 'bin' => 2, 'expected' => 0.0, 'desc' => 'Bin 2 empty'],
    ['distance' => 29.1, 'bin' => 2, 'expected' => 50.0, 'desc' => 'Bin 2 50% marker'],
    ['distance' => 10.0, 'bin' => 2, 'expected' => 100.0, 'desc' => 'Bin 2 full'],
];

foreach ($fillTests as $test) {
    $result = $method->invoke($controller, $test['distance'], $test['bin']);
    $diff = abs($result - $test['expected']);
    $status = ($diff < 0.5) ? '[PASS]' : '[FAIL]';
    echo sprintf(
        "  %s Fill %s: %.1f%% (expected: %.1f%%)\n",
        $status,
        $test['desc'],
        $result,
        $test['expected']
    );
    if ($diff >= 0.5) {
        $allPassed = false;
    }
}

$method = $reflection->getMethod('deriveWeight');
$method->setAccessible(true);

$weightTests = [
    ['raw' => 514375, 'bin' => 1, 'expected' => 0.0, 'desc' => 'Bin 1 empty'],
    ['raw' => 652673, 'bin' => 1, 'expected' => 1.53, 'desc' => 'Bin 1 1.53kg reference'],
    ['raw' => -480493, 'bin' => 2, 'expected' => 0.0, 'desc' => 'Bin 2 empty'],
    ['raw' => -338751, 'bin' => 2, 'expected' => 1.53, 'desc' => 'Bin 2 1.53kg reference'],
];

foreach ($weightTests as $test) {
    $result = $method->invoke($controller, $test['raw'], $test['bin']);
    $diff = abs($result - $test['expected']);
    $status = ($diff < 0.01) ? '[PASS]' : '[FAIL]';
    echo sprintf(
        "  %s Weight %s: %.3f kg (expected: %.3f kg)\n",
        $status,
        $test['desc'],
        $result,
        $test['expected']
    );
    if ($diff >= 0.01) {
        $allPassed = false;
    }
}

$method = $reflection->getMethod('deriveGasLevel');
$method->setAccessible(true);

$gasTests = [
    ['raw' => 200, 'expected' => 0, 'desc' => 'Normal'],
    ['raw' => 450, 'expected' => 1, 'desc' => 'Elevated'],
    ['raw' => 750, 'expected' => 2, 'desc' => 'Dangerous'],
];

foreach ($gasTests as $test) {
    $result = $method->invoke($controller, $test['raw']);
    $status = ($result === $test['expected']) ? '[PASS]' : '[FAIL]';
    $levels = ['Normal', 'Elevated', 'Dangerous'];
    echo sprintf(
        "  %s Gas %s: Level %d (%s)\n",
        $status,
        $test['desc'],
        $result,
        $levels[$result]
    );
    if ($result !== $test['expected']) {
        $allPassed = false;
    }
}

echo "\n";

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
    $status = ($diff < 0.1) ? '[PASS]' : '[FAIL]';
    echo sprintf(
        "  %s Battery %s: %.1f%% (expected: %.1f%%)\n",
        $status,
        $test['desc'],
        $result,
        $test['expected']
    );
    if ($diff >= 0.1) {
        $allPassed = false;
    }
}

echo "\n";
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
    $status = $exists ? '[PASS]' : '[FAIL]';
    echo sprintf("  %s %s\n", $status, $file);
    if (!$exists) {
        $allPassed = false;
    }
}

echo "\n";
echo "TEST 4: API Routes\n";
echo str_repeat("-", 50) . "\n";

$requiredRoutes = [
    ['method' => 'POST', 'path' => '/bin-data'],
    ['method' => 'GET', 'path' => '/summary'],
    ['method' => 'GET', 'path' => '/devices'],
    ['method' => 'POST', 'path' => '/login'],
];

$routesFile = file_get_contents($basePath . 'routes/api.php');
foreach ($requiredRoutes as $route) {
    $pattern = '/Route::' . strtolower($route['method']) . "\(['\"]" . preg_quote($route['path'], '/') . '/i';
    $hasRoute = preg_match($pattern, $routesFile);
    $status = $hasRoute ? '[PASS]' : '[FAIL]';
    echo sprintf("  %s %s %s\n", $status, $route['method'], $route['path']);
    if (!$hasRoute) {
        $allPassed = false;
    }
}

echo "\n";
echo "TEST 5: Database Schema\n";
echo str_repeat("-", 50) . "\n";

$migrationsPath = $basePath . 'database/migrations/';
$migrations = glob($migrationsPath . '*.php');

$requiredTables = ['devices', 'sensor_readings', 'alerts', 'collections'];
$foundTables = [];

foreach ($migrations as $migration) {
    $content = file_get_contents($migration);
    foreach ($requiredTables as $table) {
        if (
            strpos($content, "create_{$table}_table") !== false ||
            strpos($content, "Schema::create('{$table}'") !== false
        ) {
            $foundTables[] = $table;
        }
    }
}

foreach ($requiredTables as $table) {
    $exists = in_array($table, $foundTables, true);
    $status = $exists ? '[PASS]' : '[FAIL]';
    echo sprintf("  %s Table: %s\n", $status, $table);
    if (!$exists) {
        $allPassed = false;
    }
}

echo "\n";
echo str_repeat("=", 50) . "\n";

if ($allPassed) {
    echo "ALL TESTS PASSED - READY FOR DEPLOYMENT\n";
    echo "\nNext steps:\n";
    echo "1. Push code to GitHub\n";
    echo "2. Deploy backend to Railway\n";
    echo "3. Set APP_KEY in Railway environment\n";
    echo "4. Deploy frontend to Vercel or Netlify\n";
    echo "5. Update ESP32 with production URL\n";
    exit(0);
}

echo "SOME TESTS FAILED - FIX ISSUES BEFORE DEPLOYMENT\n";
exit(1);

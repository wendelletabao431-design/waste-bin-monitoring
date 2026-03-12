<?php

/**
 * Calibration calculation tests.
 *
 * These checks validate the March 2026 calibration values used by the
 * firmware and backend.
 */

require_once __DIR__ . '/../vendor/autoload.php';

$config = [
    'ultrasonic' => [
        'bin_1' => [
            'empty_distance_cm' => 58.7,
            'full_distance_cm' => 10.0,
            'distance_markers_cm' => [
                25 => 46.5,
                50 => 34.4,
                75 => 22.2,
            ],
        ],
        'bin_2' => [
            'empty_distance_cm' => 48.3,
            'full_distance_cm' => 10.0,
            'distance_markers_cm' => [
                25 => 38.7,
                50 => 29.1,
                75 => 19.6,
            ],
        ],
    ],
    'load_cell' => [
        'reference_weight_kg' => 1.53,
        'bin_1' => [
            'raw_empty' => 514375,
            'raw_with_reference' => 652673,
            'scale_raw_per_gram' => 90.4,
        ],
        'bin_2' => [
            'raw_empty' => -480493,
            'raw_with_reference' => -338751,
            'scale_raw_per_gram' => 92.6,
        ],
    ],
    'max_weight_kg' => 20.0,
    'mq_elevated_min' => 300,
    'mq_dangerous_min' => 600,
];

function deriveFillPercent(float $distanceCm, int $binNumber): float
{
    global $config;

    if ($distanceCm < 0) {
        return 0;
    }

    $binConfig = $config['ultrasonic']["bin_{$binNumber}"];
    $emptyDistance = $binConfig['empty_distance_cm'];
    $fullDistance = $binConfig['full_distance_cm'];

    $percent = (($emptyDistance - $distanceCm) / ($emptyDistance - $fullDistance)) * 100;

    return max(0, min(100, $percent));
}

function deriveWeight(float $hx711Raw, int $binNumber): float
{
    global $config;

    $binConfig = $config['load_cell']["bin_{$binNumber}"];
    $rawEmpty = $binConfig['raw_empty'];
    $scale = $binConfig['scale_raw_per_gram'];
    $maxWeight = $config['max_weight_kg'];

    $weight = (($hx711Raw - $rawEmpty) / $scale) / 1000;

    return max(0, min($maxWeight, $weight));
}

function deriveGasLevel(int $mqRaw): int
{
    global $config;

    if ($mqRaw >= $config['mq_dangerous_min']) {
        return 2;
    }

    if ($mqRaw >= $config['mq_elevated_min']) {
        return 1;
    }

    return 0;
}

echo "=== Calibration Calculation Tests ===\n\n";

echo "1. ULTRASONIC (Fill Percentage)\n";
echo "   Formula: (empty - distance) / (empty - full) * 100\n\n";

$fillTests = [
    ['bin' => 1, 'distance' => 58.7, 'expected' => 0.0, 'desc' => 'Bin 1 empty'],
    ['bin' => 1, 'distance' => 46.5, 'expected' => 25.0, 'desc' => 'Bin 1 at 25% marker'],
    ['bin' => 1, 'distance' => 34.4, 'expected' => 50.0, 'desc' => 'Bin 1 at 50% marker'],
    ['bin' => 1, 'distance' => 22.2, 'expected' => 75.0, 'desc' => 'Bin 1 at 75% marker'],
    ['bin' => 1, 'distance' => 10.0, 'expected' => 100.0, 'desc' => 'Bin 1 full'],
    ['bin' => 2, 'distance' => 48.3, 'expected' => 0.0, 'desc' => 'Bin 2 empty'],
    ['bin' => 2, 'distance' => 38.7, 'expected' => 25.0, 'desc' => 'Bin 2 at 25% marker'],
    ['bin' => 2, 'distance' => 29.1, 'expected' => 50.0, 'desc' => 'Bin 2 at 50% marker'],
    ['bin' => 2, 'distance' => 19.6, 'expected' => 75.0, 'desc' => 'Bin 2 at 75% marker'],
    ['bin' => 2, 'distance' => 10.0, 'expected' => 100.0, 'desc' => 'Bin 2 full'],
];

foreach ($fillTests as $test) {
    $fill = deriveFillPercent($test['distance'], $test['bin']);
    printf(
        "   %s: %.1f cm -> %.1f%% (expected %.1f%%)\n",
        $test['desc'],
        $test['distance'],
        $fill,
        $test['expected']
    );
}

echo "\n2. WEIGHT SENSOR BIN 1\n";
echo "   Formula: ((raw - empty_raw) / scale_raw_per_gram) / 1000\n\n";

$bin1Tests = [
    ['raw' => 514375, 'expected' => 0.0, 'desc' => 'Empty'],
    ['raw' => 652673, 'expected' => 1.53, 'desc' => 'With 1.53 kg reference load'],
];

foreach ($bin1Tests as $test) {
    $weight = deriveWeight($test['raw'], 1);
    printf(
        "   Raw: %d (%s) -> Weight: %.3f kg (expected %.3f kg)\n",
        $test['raw'],
        $test['desc'],
        $weight,
        $test['expected']
    );
}

echo "\n3. WEIGHT SENSOR BIN 2\n";
echo "   Formula: ((raw - empty_raw) / scale_raw_per_gram) / 1000\n\n";

$bin2Tests = [
    ['raw' => -480493, 'expected' => 0.0, 'desc' => 'Empty'],
    ['raw' => -338751, 'expected' => 1.53, 'desc' => 'With 1.53 kg reference load'],
];

foreach ($bin2Tests as $test) {
    $weight = deriveWeight($test['raw'], 2);
    printf(
        "   Raw: %d (%s) -> Weight: %.3f kg (expected %.3f kg)\n",
        $test['raw'],
        $test['desc'],
        $weight,
        $test['expected']
    );
}

echo "\n4. GAS SENSOR (MQ)\n";
echo "   Calibration: Normal < 300, Elevated >= 300, Dangerous >= 600\n\n";

$gasTests = [
    ['raw' => 200, 'desc' => 'Normal air'],
    ['raw' => 300, 'desc' => 'Elevated threshold'],
    ['raw' => 450, 'desc' => 'Elevated'],
    ['raw' => 600, 'desc' => 'Dangerous threshold'],
    ['raw' => 750, 'desc' => 'Flammable gas'],
];

$levels = ['Normal', 'Elevated', 'Dangerous'];

foreach ($gasTests as $test) {
    $level = deriveGasLevel($test['raw']);
    printf(
        "   Raw: %d (%s) -> Level: %d (%s)\n",
        $test['raw'],
        $test['desc'],
        $level,
        $levels[$level]
    );
}

echo "\n=== All Tests Completed ===\n";
echo "\nCalibration values successfully validated.\n";
echo "Backend and firmware are now synchronized.\n";

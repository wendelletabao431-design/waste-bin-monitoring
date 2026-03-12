<?php

/**
 * Sensor calibration constants.
 *
 * These values must match the ESP32 firmware calibration.
 * All raw-to-derived calculations happen in the backend.
 *
 * Calibration source: March 2026 bench calibration.
 */

return [
    /*
    |--------------------------------------------------------------------------
    | Ultrasonic Sensors (Distance -> Fill %)
    |--------------------------------------------------------------------------
    |
    | Formula:
    | fill% = (empty_distance - actual_distance) / (empty_distance - full_distance) * 100
    |
    | The 25/50/75% markers are stored for reference and validation.
    */
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

    /*
    |--------------------------------------------------------------------------
    | HX711 Load Cells (Raw -> Weight kg)
    |--------------------------------------------------------------------------
    |
    | Formula:
    | weight_kg = ((hx711_raw - raw_empty) / scale_raw_per_gram) / 1000
    |
    | Reference calibration load: 1.53 kg.
    */
    'load_cell' => [
        'reference_weight_kg' => 1.53,
        'bin_1' => [
            'raw_empty' => 514375,
            'raw_with_reference' => 652673,
            'raw_difference' => 138298,
            'scale_raw_per_gram' => 90.4,
        ],
        'bin_2' => [
            'raw_empty' => -480493,
            'raw_with_reference' => -338751,
            'raw_difference' => 141742,
            'scale_raw_per_gram' => 92.6,
        ],
    ],

    'max_weight_kg' => 20.0,

    /*
    |--------------------------------------------------------------------------
    | MQ Gas Sensor (Raw ADC -> Status Level)
    |--------------------------------------------------------------------------
    |
    | Raw values from calibration:
    | - Normal air: 100-300
    | - Alcohol spray (flammable): 600-900+
    |
    | Levels: 0 = Normal, 1 = Elevated, 2 = Dangerous.
    */
    'mq_normal_max' => 300,
    'mq_elevated_min' => 300,
    'mq_dangerous_min' => 600,

    /*
    |--------------------------------------------------------------------------
    | Battery Voltage (Voltage -> Percentage)
    |--------------------------------------------------------------------------
    |
    | Percent = (voltage - min) / (max - min) * 100
    |
    | The ESP32 should send `battery_voltage` to the backend.
    | The ADC-related values below are kept only for legacy payload fallback.
    */
    'battery_max_voltage' => 12.6,
    'battery_min_voltage' => 9.0,
    'adc_max_value' => 4095,
    'voltage_divider' => 4.21,
    'adc_reference' => 3.3,

    /*
    |--------------------------------------------------------------------------
    | Collection Detection
    |--------------------------------------------------------------------------
    |
    | A collection is detected when fill level drops significantly.
    */
    'collection_high_threshold' => 80,
    'collection_low_threshold' => 20,

    /*
    |--------------------------------------------------------------------------
    | Device Online Threshold
    |--------------------------------------------------------------------------
    |
    | Device is considered offline after this many minutes without data.
    */
    'offline_threshold_minutes' => 5,
];

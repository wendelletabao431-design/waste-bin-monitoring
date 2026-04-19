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
    | Alert thresholds:
    | fill_warning_threshold  — 50%  (distance markers for reference)
    | fill_critical_threshold — 90%
    */
    'fill_warning_threshold'  => 50,
    'fill_critical_threshold' => 90,

    'ultrasonic' => [
        'bin_1' => [
            'empty_distance_cm' => 58.0,
            'full_distance_cm'  => 14.8,
            'distance_markers_cm' => [
                50 => 34.0,   // 50% fill
                90 => 14.8,   // 90% fill (critical alert)
            ],
        ],
        'bin_2' => [
            'empty_distance_cm' => 48.0,
            'full_distance_cm'  => 13.8,
            'distance_markers_cm' => [
                50 => 29.0,   // 50% fill
                90 => 13.8,   // 90% fill (critical alert)
            ],
        ],
    ],

    /*
    |--------------------------------------------------------------------------
    | HX711 Load Cells (Raw -> Weight kg)
    |--------------------------------------------------------------------------
    |
    | Formula (authoritative, in EspController::deriveWeight):
    | weight_kg = hx711_tared / scale_raw_per_kg
    |
    | Calibration from "Raw Values = Calibration.docx":
    |   Bin 1: SCALE1 = 119,800 raw/kg
    |   Bin 2: SCALE2 = 117,786 raw/kg
    |
    | Weight alert thresholds:
    |   Bin 1: max 40 kg — warning at 20 kg (50%), critical at 36 kg (90%)
    |   Bin 2: max 20 kg — warning at 10 kg (50%), critical at 18 kg (90%)
    */
    'load_cell' => [
        'bin_1' => [
            'scale_raw_per_kg'   => 21564.0,
            'max_weight_kg'      => 40.0,
            'warning_weight_kg'  => 20.0,
            'critical_weight_kg' => 36.0,
        ],
        'bin_2' => [
            'scale_raw_per_kg'   => 21201.0,
            'max_weight_kg'      => 20.0,
            'warning_weight_kg'  => 10.0,
            'critical_weight_kg' => 18.0,
        ],
    ],

    /*
    |--------------------------------------------------------------------------
    | MQ Gas Sensor (Raw ADC -> Status Level)
    |--------------------------------------------------------------------------
    |
    | Raw values from calibration:
    | - Normal air: 100-499
    | - Flammable / dangerous: 500+
    |
    | Level returned: 0 = Normal, 1 = Dangerous
    */
    'mq_dangerous_min' => 500,

    /*
    |--------------------------------------------------------------------------
    | Battery Voltage (Voltage -> Percentage)
    |--------------------------------------------------------------------------
    |
    | Percent = (voltage - min) / (max - min) * 100
    |
    | The ESP32 reads pin 33 via a 4.0x voltage divider and sends
    | battery_voltage directly. The adc_* values below are kept only for
    | the legacy battery_adc fallback path in EspController.
    */
    'battery_max_voltage' => 12.6,
    'battery_min_voltage' => 9.0,
    'adc_max_value'       => 4095,
    'voltage_divider'     => 4.0,   // Matches firmware: battery_voltage = adc * 4.0f
    'adc_reference'       => 3.3,

    /*
    |--------------------------------------------------------------------------
    | Collection Detection
    |--------------------------------------------------------------------------
    |
    | A collection is detected when fill level drops significantly.
    | High threshold aligned with fill_critical_threshold (90%).
    */
    'collection_high_threshold' => 90,
    'collection_low_threshold'  => 20,

    /*
    |--------------------------------------------------------------------------
    | Device Online Threshold
    |--------------------------------------------------------------------------
    |
    | Device is considered offline after this many minutes without data.
    */
    'offline_threshold_minutes' => 5,
];

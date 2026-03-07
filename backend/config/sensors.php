<?php

/**
 * Sensor Calibration Constants
 * 
 * These values must match the ESP32 firmware calibration.
 * All raw-to-derived calculations happen in the backend.
 * 
 * CALIBRATION SOURCE: Raw Values = Calibration.docx
 * Date Calibrated: From hardware testing
 */

return [
    /*
    |--------------------------------------------------------------------------
    | Ultrasonic Sensor (Distance → Fill %)
    |--------------------------------------------------------------------------
    | EMPTY_DISTANCE_CM: Reading when bin is completely empty (59 cm)
    | FULL_DISTANCE_CM: Reading when bin is completely full (4 cm)
    | OFFSET_CM: Correction factor for sensor placement (3.5 cm)
    | 
    | Formula: fill% = (empty - distance - offset) / (empty - full) * 100
    | Note: Formula applied in controller, offset optional
    */
    'empty_distance_cm' => 59.0,
    'full_distance_cm' => 4.0,
    'offset_cm' => 3.5,

    /*
    |--------------------------------------------------------------------------
    | HX711 Load Cell Bin 1 (Raw → Weight kg)
    |--------------------------------------------------------------------------
    | RAW_EMPTY_BIN1: Raw reading when bin 1 is empty (451,977)
    | SCALE_BIN1: Calibration factor for bin 1 (119,800 raw units per kg)
    | MAX_WEIGHT_KG: Maximum capacity of the bin
    |
    | Formula: weight_kg = (hx711_raw - raw_empty) / scale
    */
    'raw_empty_bin1' => 451977,
    'scale_bin1' => 119800.0,
    
    /*
    |--------------------------------------------------------------------------
    | HX711 Load Cell Bin 2 (Raw → Weight kg)
    |--------------------------------------------------------------------------
    | RAW_EMPTY_BIN2: Raw reading when bin 2 is empty (-491,000)
    | SCALE_BIN2: Calibration factor for bin 2 (117,786 raw units per kg)
    |
    | Formula: weight_kg = (hx711_raw - raw_empty) / scale
    */
    'raw_empty_bin2' => -491000,
    'scale_bin2' => 117786.0,
    
    'max_weight_kg' => 20.0,

    /*
    |--------------------------------------------------------------------------
    | MQ Gas Sensor (Raw ADC → Status Level)
    |--------------------------------------------------------------------------
    | MQ_NORMAL_MAX: Maximum value for normal air (300)
    | MQ_ELEVATED_MIN: Minimum value for elevated levels (300)
    | MQ_DANGEROUS_MIN: Minimum value for dangerous/flammable (600)
    |
    | Raw values from calibration:
    | - Normal air: 100-300
    | - Alcohol spray (flammable): 600-900+
    |
    | Levels: 0 = Normal, 1 = Elevated, 2 = Dangerous (Flammable)
    */
    'mq_normal_max' => 300,
    'mq_elevated_min' => 300,
    'mq_dangerous_min' => 600,

    /*
    |--------------------------------------------------------------------------
    | Battery Voltage (Voltage → Percentage)
    |--------------------------------------------------------------------------
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
    | A "collection" is detected when fill level drops significantly
    */
    'collection_high_threshold' => 80,  // Was above this %
    'collection_low_threshold' => 20,  // Dropped below this %

    /*
    |--------------------------------------------------------------------------
    | Device Online Threshold
    |--------------------------------------------------------------------------
    | Device is considered offline after this many minutes without data
    */
    'offline_threshold_minutes' => 5,
];

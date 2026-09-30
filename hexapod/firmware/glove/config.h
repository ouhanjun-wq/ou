// Pins of the gesture glove: Seeed Studio XIAO ESP32S3. Wiring: docs/assembly-guide.md, figure 1.
#pragma once
#include <Arduino.h>

// Flex sensors: 3V3 -> flex sensor -> pin -> 47 kOhm -> GND (ADC1 pins only: ADC2 does not work
// while the radio is on).        thumb  index  middle ring  little
const int PIN_FLEX[5] = {D0, D1, D2, D3, D8};

// MPU6050 (GY-521) on I2C, address 0x68 (AD0 not connected / low).
const int PIN_SDA = D4;
const int PIN_SCL = D5;

const int PIN_BUTTON = D9;       // push button to GND (internal pull-up)
const int PIN_VIBRATE = D10;     // vibration motor module IN (high = on)
const int PIN_LED = LED_BUILTIN; // yellow user LED, on when LOW
const bool LED_ACTIVE_LOW = true;

const uint32_t SERIAL_BAUD = 115200;
const uint32_t SAMPLE_US = 10000;   // sensors 100 Hz
const uint32_t SEND_MS = 20;        // packets 50 Hz

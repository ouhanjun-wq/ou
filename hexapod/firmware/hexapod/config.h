// Pins and fixed settings of the hexapod's ESP32 (the ESP32 DevKit that came with the kit).
// Wiring: docs/assembly-guide.md, figure 2. Everything else is a parameter (SHOW / SAVE).
#pragma once
#include <Arduino.h>

// I2C to the PCA9685 servo chip(s) on the servo board (ESP32 default pins).
const int PIN_SDA = 21;
const int PIN_SCL = 22;

// Battery voltage: the learning kit's voltage sensor module (S pin), 5:1 divider.
// GPIO 34 is input-only, so it never clashes with a servo. -1 = not fitted.
const int PIN_VBAT = 34;

// Optional HC-SR04 on the kit's ultrasonic bracket. ECHO needs a 1k / 2k divider (5 V -> 3.3 V).
// Pick two free pins of your board (not used by servos), e.g. 25 and 26. -1 = not fitted.
const int PIN_TRIG = -1;
const int PIN_ECHO = -1;

// Blue LED on most ESP32 DevKits. Not driven if a servo uses the same pin (GPIO boards).
const int PIN_LED = 2;

const uint32_t SERIAL_BAUD = 115200;
const uint32_t LOOP_US = 20000;          // control loop 50 Hz
const uint32_t STATUS_MS = 200;          // status packet to the glove, 5 Hz

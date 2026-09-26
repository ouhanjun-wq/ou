// Pins and fixed settings for the Arduino Uno R3 + USB Host Shield 2.0.
// Wiring: docs/assembly-guide.md, figures 1-3.
#pragma once
#include <Arduino.h>

// The USB Host Shield uses D9 (INT), D10 (SS) and D11-D13 (SPI); some versions also D7.
// Servos: PCA9685 servo driver module on I2C (SDA = A4, SCL = A5), J1 .. J6 on channels 0 .. 5.
const uint8_t SERVO_CH0 = 0;
const uint8_t PIN_VSENSE = A3;          // servo supply after the E-stop, through the voltage sensor module (5:1)
// Optional offline voice module: its TX goes to D0 (RX), sharing the USB serial port.
// Set the module to the same baud rate. Unplug it while uploading a sketch.
const uint32_t SERIAL_BAUD = 9600;
const uint32_t LOOP_US = 20000;         // control loop 50 Hz

// 0 = normal use (G7 Pro gamepad + motion commands)
// 1 = calibration build: no gamepad, all setup commands (PULSE, MARK, LIM, GEO, POSE, SAVE ...)
//     The Uno's 32 KB of flash cannot hold the gamepad library and the setup commands together.
#ifndef SETUP_MODE
#define SETUP_MODE 0
#endif

// EEPROM (1 KB): parameters at 0, waypoints from 256 (count byte, then 6 x int16 each).
const int EE_PARAMS = 0;
const int EE_WAYPOINTS = 256;
const int WAYPOINT_CAPACITY = 60;

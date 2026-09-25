// Butterfly flight controller — fixed hardware configuration.
// Tunable values live in params.cpp (changeable over USB / radio, saved to flash).
#pragma once
#include <Arduino.h>

// ---------------- Board: Seeed Studio XIAO ESP32S3 ----------------
constexpr int PIN_VBAT     = D0;   // GPIO1  battery divider (ADC1)
constexpr int PIN_SERVO_L  = D1;   // GPIO2  left wing servo signal
constexpr int PIN_SERVO_R  = D2;   // GPIO3  right wing servo signal
constexpr int PIN_IMU_CS   = D3;   // GPIO4  ICM-42688-P chip select
constexpr int PIN_IMU_SCK  = D8;   // GPIO7
constexpr int PIN_IMU_MISO = D9;   // GPIO8
constexpr int PIN_IMU_MOSI = D10;  // GPIO9
constexpr int PIN_CHG_DETECT = D6; // GPIO43 Type-C charger VBUS via 100k/200k divider (HIGH = charging)
constexpr int PIN_LED      = LED_BUILTIN;  // GPIO21, active LOW

// ---------------- Timing ----------------
constexpr float IMU_HZ   = 1000.0f;  // sensor + attitude loop
constexpr int   CTRL_DIV = 5;        // control loop = IMU_HZ / CTRL_DIV = 200 Hz
constexpr float CTRL_HZ  = IMU_HZ / CTRL_DIV;

// Servo PWM frame rate: 333 Hz for digital servos, 50 Hz for analog servos.
constexpr int SERVO_PWM_HZ   = 333;
constexpr int SERVO_PWM_BITS = 14;
constexpr int SERVO_US_MIN   = 500;
constexpr int SERVO_US_MAX   = 2500;
constexpr int SERVO_US_MID   = 1500;

// Telemetry over radio
constexpr uint32_t TELEMETRY_PERIOD_MS = 50;  // 20 Hz

// Stroke averaging window capacity (samples at CTRL_HZ). 128 @ 200 Hz covers f >= 1.6 Hz.
constexpr int STROKE_MAX_N = 128;

// Firmware version string printed on boot
constexpr const char* FW_VERSION = "butterfly_fc 0.1.0";

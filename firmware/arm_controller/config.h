// Pins and fixed settings. Wiring: docs/assembly-guide.md, figures 1-3.
#pragma once
#include <stdint.h>

// ESP32 DevKit (ESP32-WROOM-32E, original ESP32: Classic Bluetooth + BLE for the G7 Pro)
constexpr int PIN_SDA = 21;             // I2C -> PCA9685 + INA226
constexpr int PIN_SCL = 22;
constexpr int PIN_RELAY = 25;           // relay module IN (high-level trigger): HIGH = servo power on
constexpr int PIN_BUZZER = 26;          // active buzzer module: HIGH = beep
constexpr int PIN_LED = 2;              // status LED (on-board on most DevKits)
constexpr int PIN_VOICE_RX = 16;        // UART2 RX <- voice module TX (optional)
constexpr int PIN_VOICE_TX = 17;        // UART2 TX -> voice module RX (optional)
constexpr uint32_t VOICE_BAUD = 9600;   // set your voice module to the same baud rate

constexpr uint8_t PCA9685_ADDR = 0x41;  // solder bridge A0 on the PCA9685 board (INA226 uses 0x40)
constexpr uint8_t INA226_ADDR = 0x40;
constexpr uint8_t SERVO_CH[6] = {0, 1, 2, 3, 4, 5};   // PCA9685 channel of J1..J6
constexpr float PWM_HZ = 50.0f;         // standard servo frame
constexpr uint32_t LOOP_US = 20000;     // control loop 50 Hz

#ifndef ENABLE_WEB
#define ENABLE_WEB 1                    // phone page over the ESP32's own Wi-Fi access point
#endif
constexpr const char* AP_SSID = "RobotArm";
constexpr const char* AP_PASS = "robotarm123";   // at least 8 characters

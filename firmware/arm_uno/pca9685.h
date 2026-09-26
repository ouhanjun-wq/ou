// PCA9685 16-channel servo driver module on I2C (Uno: SDA = A4, SCL = A5), address 0x40.
// J1..J6 sit on channels SERVO_CH0 .. SERVO_CH0 + 5 (config.h).
// Direct TWI register code instead of the Wire library: the Uno's flash is nearly full.
#pragma once
#include <stdint.h>

namespace pca {

constexpr uint8_t PRESCALE = 121;       // 25 MHz / (4096 * (121 + 1)) = 50 Hz servo frame

// Pulse (us) -> 12-bit count. One count lasts (PRESCALE + 1) / oscillator. The internal
// oscillator is nominally 25 000 kHz, but modules run anywhere from about 23 to 27 MHz:
// the OSC command sets the real value from the frame rate you measure.
inline uint16_t usToCount(float us, uint16_t osc_khz) {
  float c = us * osc_khz / (1000.0f * (PRESCALE + 1)) + 0.5f;
  if (c < 0) c = 0;
  if (c > 4095) c = 4095;
  return (uint16_t)c;
}

// Oscillator (kHz) from the frame rate (Hz) measured on any servo signal pin.
inline long oscFromFrameHz(float hz) { return (long)(hz * 4096.0f * (PRESCALE + 1) / 1000.0f + 0.5f); }

#if defined(__AVR__)
#include <avr/io.h>

class Pca9685 {
 public:
  bool begin() {
    PORTC |= _BV(PC4) | _BV(PC5);                 // weak pull-ups on SDA / SCL (the module has 10k)
    TWSR = 0;
    TWBR = (F_CPU / 100000UL - 16) / 2;           // 100 kHz: safe over 20 cm jumper wires
    const uint8_t sleep[] = {0x00, 0x30};         // MODE1: sleep + register auto-increment
    const uint8_t pre[] = {0xFE, PRESCALE};       // the prescaler can only be set while asleep
    const uint8_t wake[] = {0x00, 0x20};
    const uint8_t restart[] = {0x00, 0xA0};
    bool ok = send(sleep, 2) && send(pre, 2) && send(wake, 2);
    for (volatile uint16_t i = 0; i < 4000; ++i) {}   // oscillator start-up (> 500 us)
    return ok && send(restart, 2) && allOff();
  }
  // No pulses on any channel: the servos stop driving (same as a detached servo).
  bool allOff() {
    const uint8_t off[] = {0xFA, 0, 0, 0, 0x10};  // ALL_LED_ON/OFF, full-off bit
    return send(off, sizeof(off));
  }
  // n channels from ch0 in one transfer; the outputs change together at the STOP.
  bool write(uint8_t ch0, const uint16_t* count, uint8_t n) {
    uint8_t b[1 + 4 * 6];
    if (n > 6) n = 6;
    b[0] = 0x06 + 4 * ch0;                        // LEDn_ON_L
    for (uint8_t i = 0; i < n; ++i) {
      b[1 + 4 * i] = 0;                           // pulse starts at count 0 ...
      b[2 + 4 * i] = 0;
      b[3 + 4 * i] = count[i] & 0xFF;             // ... and ends at count[i]
      b[4 + 4 * i] = (count[i] >> 8) & 0x0F;
    }
    return send(b, 1 + 4 * n);
  }

 private:
  static bool wait() {
    for (uint16_t n = 1; n; ++n)                  // ~20 ms timeout
      if (TWCR & _BV(TWINT)) return true;
    return false;
  }
  static bool put(uint8_t v, uint8_t expect) {
    TWDR = v;
    TWCR = _BV(TWINT) | _BV(TWEN);
    return wait() && (TWSR & 0xF8) == expect;
  }
  static bool send(const uint8_t* b, uint8_t n) {
    TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);   // START
    bool ok = wait() && (TWSR & 0xF8) == 0x08 && put(0x40 << 1, 0x18);   // address + W, ACK
    for (uint8_t i = 0; ok && i < n; ++i) ok = put(b[i], 0x28);          // data, ACK
    TWCR = _BV(TWINT) | _BV(TWSTO) | _BV(TWEN);   // STOP
    for (uint16_t t = 1; t && (TWCR & _BV(TWSTO)); ++t) {}
    if (!ok) TWCR = 0;                            // release the bus; the next START re-enables it
    return ok;
  }
};
#endif

}  // namespace pca

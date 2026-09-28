#include "servo_out.h"

namespace servo {
Table T;
}

#ifdef ARDUINO
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include "driver/mcpwm.h"   // legacy MCPWM driver: servos 17 and 18 when all 16 LEDC channels are busy

namespace servo {

static int last_[NUM] = {};
static uint8_t pcaMask_ = 0;          // bit i = PCA9685 found at 0x40 + i

// ---------------- PCA9685 ----------------
constexpr uint8_t PCA_MODE1 = 0x00, PCA_MODE2 = 0x01, PCA_LED0 = 0x06, PCA_ALL_OFF_H = 0xFD,
                  PCA_PRESCALE = 0xFE;
constexpr uint8_t PCA_PRESCALE_50HZ = 121;                 // 25 MHz / (4096 * (121 + 1)) = 50.03 Hz
constexpr float PCA_PERIOD_US = (PCA_PRESCALE_50HZ + 1) * 4096 / 25.0f;

static void pcaWrite8(uint8_t addr, uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission();
}

static void pcaInit(uint8_t addr) {
  pcaWrite8(addr, PCA_MODE1, 0x30);                 // sleep + auto-increment (prescale needs sleep)
  pcaWrite8(addr, PCA_PRESCALE, PCA_PRESCALE_50HZ);
  pcaWrite8(addr, PCA_MODE2, 0x04);                 // totem-pole outputs
  pcaWrite8(addr, PCA_ALL_OFF_H, 0x10);             // every channel fully off
  pcaWrite8(addr, PCA_MODE1, 0x20);                 // wake
  delay(1);
  pcaWrite8(addr, PCA_MODE1, 0xA0);                 // restart + auto-increment
}

static void pcaChannel(uint8_t addr, uint8_t ch, int us) {
  uint16_t off = 0x1000;                            // bit 12 = full off
  if (us > 0) off = uint16_t(lroundf(us * 4096.0f / PCA_PERIOD_US)) & 0x0FFF;
  Wire.beginTransmission(addr);
  Wire.write(uint8_t(PCA_LED0 + 4 * (ch & 15)));
  Wire.write(0);
  Wire.write(0);
  Wire.write(uint8_t(off & 0xFF));
  Wire.write(uint8_t(off >> 8));
  Wire.endTransmission();
}

bool pcaFound(uint8_t addr) {
  return addr >= 0x40 && addr <= 0x47 && (pcaMask_ & (1 << (addr - 0x40)));
}

// ---------------- direct GPIO: 16 LEDC channels, then 6 MCPWM outputs ----------------
constexpr int LEDC_SLOTS = 16, MCPWM_SLOTS = 6;
constexpr int PWM_HZ = 50, LEDC_BITS = 16;
static int8_t slotPin_[LEDC_SLOTS + MCPWM_SLOTS];
static bool mcpwmTimerReady_[3] = {};
static bool slotsReady_ = false;

static void slotsInit() {
  if (slotsReady_) return;
  for (auto& p : slotPin_) p = -1;
  slotsReady_ = true;
}

static int findSlot(uint8_t pin) {
  slotsInit();
  for (int s = 0; s < LEDC_SLOTS + MCPWM_SLOTS; ++s)
    if (slotPin_[s] == pin) return s;
  return -1;
}

static int attach(uint8_t pin) {
  int s = findSlot(pin);
  if (s >= 0) return s;
  for (s = 0; s < LEDC_SLOTS + MCPWM_SLOTS; ++s)
    if (slotPin_[s] < 0) break;
  if (s == LEDC_SLOTS + MCPWM_SLOTS) return -1;     // out of PWM channels
  if (s < LEDC_SLOTS) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    if (!ledcAttachChannel(pin, PWM_HZ, LEDC_BITS, s)) return -1;
#else
    ledcSetup(s, PWM_HZ, LEDC_BITS);
    ledcAttachPin(pin, s);
#endif
  } else {
    const int i = s - LEDC_SLOTS, timer = i / 2;
    mcpwm_gpio_init(MCPWM_UNIT_0, mcpwm_io_signals_t(MCPWM0A + i), pin);
    if (!mcpwmTimerReady_[timer]) {
      mcpwm_config_t c = {};
      c.frequency = PWM_HZ;
      c.counter_mode = MCPWM_UP_COUNTER;
      c.duty_mode = MCPWM_DUTY_MODE_0;
      mcpwm_init(MCPWM_UNIT_0, mcpwm_timer_t(timer), &c);
      mcpwmTimerReady_[timer] = true;
    }
  }
  slotPin_[s] = pin;
  return s;
}

static void slotWrite(int s, int us) {
  if (s < LEDC_SLOTS) {
    const uint32_t duty = uint32_t(uint64_t(us) * PWM_HZ * (1u << LEDC_BITS) / 1000000u);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(slotPin_[s], duty);
#else
    ledcWrite(s, duty);
#endif
  } else {
    const int i = s - LEDC_SLOTS;
    mcpwm_set_duty_in_us(MCPWM_UNIT_0, mcpwm_timer_t(i / 2), mcpwm_operator_t(i % 2), uint32_t(us));
  }
}

static void release(int s) {
  const int pin = slotPin_[s];
  if (pin < 0) return;
  slotWrite(s, 0);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (s < LEDC_SLOTS) ledcDetach(pin);
#else
  if (s < LEDC_SLOTS) ledcDetachPin(pin);
#endif
  pinMode(pin, OUTPUT);                             // hand the pin back to plain GPIO, held low
  digitalWrite(pin, LOW);
  slotPin_[s] = -1;
}

// ---------------- public ----------------
void begin(int sda, int scl) {
  slotsInit();
  Wire.begin(sda, scl, 400000);
  pcaMask_ = 0;
  for (uint8_t a = 0x40; a <= 0x47; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      pcaMask_ |= 1 << (a - 0x40);
      pcaInit(a);
    }
  }
  if (!load()) {
    if (pcaMask_) defaultPca(T);
    else defaultGpio(T);
  }
}

bool load() {
  Preferences prefs;
  if (!prefs.begin("hexapod", true)) return false;
  Table t;
  const bool ok = prefs.getBytesLength("srv") == sizeof(Table) &&
                  prefs.getBytes("srv", &t, sizeof(Table)) == sizeof(Table) && t.version == TABLE_VERSION;
  prefs.end();
  if (ok) T = t;
  return ok;
}

bool save() {
  Preferences prefs;
  if (!prefs.begin("hexapod", false)) return false;
  T.version = TABLE_VERSION;
  const bool ok = prefs.putBytes("srv", &T, sizeof(Table)) == sizeof(Table);
  prefs.end();
  return ok;
}

void writeUs(int n, int us) {
  if (n < 0 || n >= NUM) return;
  if (us > 0) us = constrain(us, US_MIN, US_MAX);
  if (us == last_[n]) return;                      // unchanged: skip the I2C / register write
  const Map& m = T.m[n];
  if (m.kind == PCA) {
    if (pcaFound(m.a)) pcaChannel(m.a, m.b, us);
  } else if (m.kind == GPIO) {
    int s = findSlot(m.a);
    if (s < 0 && us > 0) s = attach(m.a);
    if (s >= 0) slotWrite(s, us);
  }
  last_[n] = us;
}

void writeDeg(int n, float deg, float us_per_deg) {
  if (n < 0 || n >= NUM) return;
  writeUs(n, pulseUs(deg, T.m[n], us_per_deg));
}

void off(int n) { writeUs(n, 0); }

void allOff() {
  for (int n = 0; n < NUM; ++n) last_[n] = 0;
  for (uint8_t a = 0x40; a <= 0x47; ++a)
    if (pcaFound(a)) pcaWrite8(a, PCA_ALL_OFF_H, 0x10);
  for (int s = 0; s < LEDC_SLOTS + MCPWM_SLOTS; ++s) release(s);
}

int lastUs(int n) { return (n >= 0 && n < NUM) ? last_[n] : 0; }

int scanI2c(void (*found)(uint8_t addr)) {
  int count = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      ++count;
      if (found) found(a);
    }
  }
  return count;
}

void probeGpio(uint8_t pin, int us) {
  int s = findSlot(pin);
  if (us <= 0) {
    if (s >= 0) release(s);
    return;
  }
  if (s < 0) s = attach(pin);
  if (s >= 0) slotWrite(s, constrain(us, US_MIN, US_MAX));
}

void probePca(uint8_t addr, uint8_t ch, int us) {
  if (pcaFound(addr)) pcaChannel(addr, ch, us > 0 ? constrain(us, US_MIN, US_MAX) : 0);
}

}  // namespace servo
#endif  // ARDUINO

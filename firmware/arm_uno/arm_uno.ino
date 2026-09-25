// 6-servo robot arm (5 joints + gripper) on an Arduino Uno R3, controlled by a
// GameSir G7 Pro through a USB Host Shield 2.0.
//
// Board: Arduino Uno R3. Library: "USB Host Shield Library 2.0" (Library Manager).
// G7 Pro: USB cable (or its 2.4G receiver) into the shield's USB-A port, PC / XInput mode.
// Not detected? The library only accepts known USB IDs: see firmware/README.md ("VID / PID").
//
// Gamepad (Xbox layout)            JOINT mode              XYZ mode (straight lines)
//   Left stick  left / right       J1 base                 tool left / right
//   Left stick  up / down          J2 shoulder             tool forward / back
//   Right stick up / down          J3 elbow                tool up / down
//   Right stick left / right       J4 wrist pitch          tool pitch
//   D-pad left / right             J5 wrist roll           J5 wrist roll
//   RT / LT                        close / open the gripper (releasing RT backs off a little)
//   Menu (≡)  servos on / park + off     View (⧉)  JOINT <-> XYZ
//   Y  home   B  stop   LB / RB  speed - / +   D-pad down  delete last waypoint
//   A  record waypoint (hold 2 s: clear all)   X  play once (hold 1 s: loop)
//
// Text commands (commands.h) on the serial port, 9600 baud: from a PC / AI over USB, or from an
// offline voice module whose TX is wired to D0 (RX).
#include <EEPROM.h>
#include <SPI.h>
#include <Servo.h>

#include "config.h"
#if !SETUP_MODE
#include <XBOXUSB.h>
#endif
#define ARM_SETUP_COMMANDS SETUP_MODE

#include "commands.h"

#if SETUP_MODE
#define PAD_CONNECTED false
#else
USB Usb;
XBOXUSB Pad(&Usb);   // XInput (Xbox 360 protocol); the G7 Pro's View = BACK, Menu = START
#define PAD_CONNECTED (Pad.Xbox360Connected)
#define RUMBLE(v) Pad.setRumbleOn((v), (v))
#endif

// Replies go straight to Serial, piece by piece (no printf: saves flash).
class SerialOut : public arm::Out {
 public:
  void text(const char* pgm) override { Serial.print(reinterpret_cast<const __FlashStringHelper*>(pgm)); }
  void num(long v) override { Serial.print(v); }
  void dec(float v, uint8_t digits) override {   // integer maths: smaller than Print's float code
    long scale = 1;
    for (uint8_t i = 0; i < digits; ++i) scale *= 10;
    long n = lroundf(v * scale);
    if (n < 0) { Serial.print('-'); n = -n; }
    Serial.print(n / scale);
    if (!digits) return;
    Serial.print('.');
    for (long f = n % scale, d = scale / 10; d > 0; d /= 10) { Serial.print((char)('0' + f / d)); f %= d; }
  }
  void end() override { Serial.println(); }
};

// Waypoints in EEPROM: the Uno's 2 KB of RAM cannot hold them.
class EepromStore : public arm::WaypointStore {
 public:
  void begin() {
    n_ = EEPROM.read(EE_WAYPOINTS);
    if (n_ > WAYPOINT_CAPACITY) {   // fresh chip reads 0xFF
      n_ = 0;
      EEPROM.update(EE_WAYPOINTS, 0);
    }
  }
  int count() const override { return n_; }
  int capacity() const override { return WAYPOINT_CAPACITY; }
  void get(int i, float* q) const override {
    for (int j = 0; j < NJ; ++j) {
      int16_t v;
      EEPROM.get(addr(i, j), v);
      q[j] = v / 10.0f;               // stored in tenths of a degree
    }
  }
  bool append(const float* q) override {
    if (n_ >= WAYPOINT_CAPACITY) return false;
    for (int j = 0; j < NJ; ++j) {
      const int16_t v = (int16_t)lroundf(q[j] * 10.0f);
      EEPROM.put(addr(n_, j), v);
    }
    EEPROM.update(EE_WAYPOINTS, ++n_);
    return true;
  }
  void removeLast() override {
    if (n_ > 0) EEPROM.update(EE_WAYPOINTS, --n_);
  }
  void clear() override {
    n_ = 0;
    EEPROM.update(EE_WAYPOINTS, 0);
  }

 private:
  static int addr(int i, int j) { return EE_WAYPOINTS + 2 + (i * NJ + j) * 2; }
  uint8_t n_ = 0;
};

Params params;
EepromStore store;
arm::ArmCore core(params, store);
arm::Sensors sensors;
Servo servo[NJ];

static bool saveParams(const Params& p) {
  EEPROM.put(EE_PARAMS, p);           // put() only rewrites bytes that changed
  return true;
}

SerialOut out;

static arm::PadInput readPad() {
  arm::PadInput in;
#if !SETUP_MODE
  if (!PAD_CONNECTED) return in;
  in.valid = true;
  in.lx = Pad.getAnalogHat(LeftHatX) / 32768.0f;
  in.ly = Pad.getAnalogHat(LeftHatY) / 32768.0f;    // XInput: stick up = positive
  in.rx = Pad.getAnalogHat(RightHatX) / 32768.0f;
  in.ry = Pad.getAnalogHat(RightHatY) / 32768.0f;
  in.lt = Pad.getButtonPress(LT) / 255.0f;
  in.rt = Pad.getButtonPress(RT) / 255.0f;
  // Library button for each arm::PadButton (PB_A .. PB_MENU)
  static const uint8_t kButtons[arm::PB_COUNT] PROGMEM = {A, B, X, Y, LB, RB, UP, DOWN, LEFT, RIGHT, BACK, START};
  for (uint8_t i = 0; i < arm::PB_COUNT; ++i) in.btn[i] = Pad.getButtonPress((ButtonEnum)pgm_read_byte(&kButtons[i]));
#endif
  return in;
}

static void applyOutputs() {
  static bool attached = false;
  if (core.power) {
    if (!attached) {
      // Attach and set the first pulse with interrupts off, so the very first pulse is
      // already the park pose (Servo.h would otherwise send 1500 us once).
      noInterrupts();
      for (int j = 0; j < NJ; ++j) {
        servo[j].attach(SERVO_PIN[j], 400, 2700);
        servo[j].writeMicroseconds((int)lroundf(core.pulseUs(j)));
      }
      interrupts();
      attached = true;
    }
    for (int j = 0; j < NJ; ++j) servo[j].writeMicroseconds((int)lroundf(core.pulseUs(j)));
  } else if (attached) {
    for (int j = 0; j < NJ; ++j) servo[j].detach();   // no pulses: the servos stop driving
    attached = false;
  }
}

static void handleLine(char* line) {
  arm::runCommand(core, sensors, line, out, saveParams);
}

static void pollStream(Stream& s, char* buf, uint8_t& len, uint8_t cap) {
  while (s.available()) {
    const char c = (char)s.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[len] = '\0';
      len = 0;
      if (buf[0]) handleLine(buf);
    } else if (len < cap - 1) {
      buf[len++] = c;
    }
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  EEPROM.get(EE_PARAMS, params);
  if (params.magic != PARAMS_MAGIC || params.version != PARAMS_VERSION) {
    paramsDefaults(params);
    Serial.println(F("no saved parameters: DEFAULTS (calibrate first!)"));
  }
  core.reset();
  store.begin();
  Serial.print(store.count());
  Serial.println(F(" waypoints in EEPROM"));
#if SETUP_MODE
  Serial.println(F("CALIBRATION build (SETUP_MODE 1): no gamepad. ON, then PULSE / MARK / LIM / GEO / SAVE."));
#else
  if (Usb.Init() == -1) Serial.println(F("USB Host Shield NOT found (shield seated? 5V?)"));
  Serial.println(F("ready. G7 Pro: XInput mode, cable into the shield. Menu = servos on. HELP lists commands."));
#endif
}

void loop() {
  static uint32_t last = micros(), rumbleUntil = 0;
  static bool padWas = false;
  static char usbBuf[64];
  static uint8_t usbLen = 0;

#if !SETUP_MODE
  Usb.Task();
#endif
  pollStream(Serial, usbBuf, usbLen, sizeof(usbBuf));

  const bool pad = PAD_CONNECTED;
  if (pad != padWas) {
    padWas = pad;
    Serial.println(pad ? F("gamepad connected") : F("gamepad disconnected (arm holds its pose)"));
  }

  const uint32_t now = micros();
  if (now - last < LOOP_US) return;
  const float dt = min((now - last) * 1e-6f, 0.05f);
  last = now;

  sensors.ok = true;
  sensors.volts = analogRead(PIN_VSENSE) * (5.0f / 1023.0f) * params.vdiv_x100 / 100.0f;
  core.update(readPad(), sensors, dt);
  applyOutputs();

  if (core.event) {
    Serial.print(F("[arm] "));
    Serial.println(reinterpret_cast<const __FlashStringHelper*>(core.event));
  }
#if !SETUP_MODE
  if (pad && core.rumbleMs) {
    RUMBLE(0x60);
    rumbleUntil = millis() + core.rumbleMs;
  }
  if (rumbleUntil && (int32_t)(millis() - rumbleUntil) >= 0) {
    rumbleUntil = 0;
    if (pad) RUMBLE(0);
  }
#else
  (void)rumbleUntil;
#endif
}

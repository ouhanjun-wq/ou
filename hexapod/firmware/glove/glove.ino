// Gesture glove for the hexapod: 5 flex sensors + MPU6050 on a Seeed XIAO ESP32S3, sending
// ESP-NOW packets to the robot 50 times a second.
//
// Board: "XIAO_ESP32S3". No extra libraries.
//
// Button:  short press = arm / disarm (disarmed: the robot always stands still)
//          hold 2 s    = calibrate: when it buzzes, hold the hand OPEN and FLAT (2 buzzes when
//                        done), then make a FIST (long buzz = saved; 3 buzzes = failed, again)
// Gestures (hand flat = neutral):
//   open hand   walk: tilt forward / back = forward / back, tilt left / right = turn
//   index only  crab: tilt forward / back = forward / back, tilt left / right = sideways
//   victory     the robot's body tilts like your hand
//   fist        stop
//   thumb up (or little finger only) for 1 s   stand up / sit down
//   rock (index + little finger) for 0.6 s     next gait
// Serial (115200): SHOW  CAL  THUMB 0|1  AXIS p r  LINK id  DEAD deg  FULL deg  SAVE  DEFAULTS
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "config.h"
#include "gesture.h"
#include "protocol.h"

using namespace hx;

static GloveParams P;
static Preferences prefs;
static Fingers fingers;
static GestureFilter filter;
static Tilt tilt;

static float flexRaw[NF];
static float bend[NF];
static float ax, ay, az, gx, gy, gz;
static bool imuOk = false;
static bool armed = false;
static bool live = false;

// ---------------------------------------------------------------- MPU6050
constexpr uint8_t MPU = 0x68;

static void mpuWrite(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(MPU);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission();
}

static bool mpuBegin() {
  Wire.beginTransmission(MPU);
  if (Wire.endTransmission() != 0) return false;
  mpuWrite(0x6B, 0x01);   // wake, clock = gyro X PLL
  delay(10);
  mpuWrite(0x1A, 0x03);   // DLPF 44 Hz
  mpuWrite(0x1B, 0x08);   // gyro +-500 deg/s (65.5 LSB per deg/s)
  mpuWrite(0x1C, 0x08);   // accel +-4 g (8192 LSB per g)
  return true;
}

static bool mpuRead() {
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MPU, 14) != 14) return false;
  int16_t v[7];
  for (int i = 0; i < 7; ++i) v[i] = (int16_t)((Wire.read() << 8) | Wire.read());
  ax = v[0] / 8192.0f;
  ay = v[1] / 8192.0f;
  az = v[2] / 8192.0f;
  gx = v[4] / 65.5f - P.gbias[0];
  gy = v[5] / 65.5f - P.gbias[1];
  gz = v[6] / 65.5f - P.gbias[2];
  return true;
}

// ---------------------------------------------------------------- vibration + LED
struct Buzzer {
  uint16_t on = 0, off = 0;
  int left = 0;
  uint32_t next = 0;
  bool high = false;

  void play(uint16_t on_ms, uint16_t off_ms, int count, uint32_t now) {
    on = on_ms;
    off = off_ms;
    left = count;
    high = false;
    next = now;
  }
  void update(uint32_t now) {
    if (left <= 0 && !high) return;
    if ((int32_t)(now - next) < 0) return;
    if (high) {
      high = false;
      --left;
      next = now + off;
    } else if (left > 0) {
      high = true;
      next = now + on;
    }
    digitalWrite(PIN_VIBRATE, high ? HIGH : LOW);
  }
  bool busy() const { return left > 0 || high; }
};
static Buzzer buzz;

static void led(bool on) { digitalWrite(PIN_LED, (on != LED_ACTIVE_LOW) ? HIGH : LOW); }

// ---------------------------------------------------------------- radio
static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static volatile uint32_t statusMs = 0;
static volatile uint8_t statusFlags = 0, statusState = 0, statusGait = 0;
static volatile uint16_t statusMv = 0;
static bool everLinked = false;

static void handleRx(const uint8_t* data, int len) {
  StatusPacket s;
  if (!hx::open(data, len, PKT_STATUS, P.link_id, s)) return;
  statusFlags = s.flags;
  statusState = s.state;
  statusGait = s.gait;
  statusMv = s.vbat_mv;
  statusMs = millis();
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
  handleRx(data, len);
}
#else
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  handleRx(data, len);
}
#endif

static bool radioBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcast, 6);
  peer.channel = WIFI_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

static bool linkOk(uint32_t now) { return statusMs != 0 && now - statusMs < 1000; }

// ---------------------------------------------------------------- calibration
enum CalStep { CAL_IDLE, CAL_GET_OPEN, CAL_OPEN, CAL_GET_FIST, CAL_FIST };
static CalStep cal = CAL_IDLE;
static uint32_t calT0 = 0;
static int calN = 0;
static float calSum[NF], calPitch, calRoll, calG[3];
static GloveParams calNew;

static void calStart(uint32_t now) {
  armed = false;
  cal = CAL_GET_OPEN;
  calT0 = now;
  buzz.play(400, 100, 1, now);
  Serial.println("CAL: hold the hand OPEN and FLAT (neutral pose)");
}

static void calReset() {
  calN = 0;
  calPitch = calRoll = 0;
  for (int f = 0; f < NF; ++f) calSum[f] = 0;
  for (int k = 0; k < 3; ++k) calG[k] = 0;
}

static void calUpdate(uint32_t now) {
  const uint32_t t = now - calT0;
  switch (cal) {
    case CAL_IDLE:
      return;
    case CAL_GET_OPEN:
    case CAL_GET_FIST:
      if (t > 1500) {
        cal = cal == CAL_GET_OPEN ? CAL_OPEN : CAL_FIST;
        calT0 = now;
        calReset();
      }
      return;
    case CAL_OPEN:
    case CAL_FIST:
      for (int f = 0; f < NF; ++f) calSum[f] += flexRaw[f];
      calPitch += atan2f(-ax, sqrtf(ay * ay + az * az)) * 57.29578f;
      calRoll += atan2f(ay, az) * 57.29578f;
      calG[0] += gx + P.gbias[0];
      calG[1] += gy + P.gbias[1];
      calG[2] += gz + P.gbias[2];
      ++calN;
      if (t < 2000) return;
      if (cal == CAL_OPEN) {
        calNew = P;
        for (int f = 0; f < NF; ++f) calNew.open[f] = calSum[f] / calN;
        calNew.pitch0 = calPitch / calN;
        calNew.roll0 = calRoll / calN;
        for (int k = 0; k < 3; ++k) calNew.gbias[k] = calG[k] / calN;
        cal = CAL_GET_FIST;
        calT0 = now;
        buzz.play(80, 120, 2, now);
        Serial.println("CAL: now make a FIST");
        return;
      }
      for (int f = 0; f < NF; ++f) calNew.fist[f] = calSum[f] / calN;
      bool ok = true;
      for (int f = 0; f < NF; ++f) {
        if (f == THUMB && !calNew.thumb) continue;
        const float d = fabsf(calNew.fist[f] - calNew.open[f]);
        Serial.printf("  finger %d: open %.0f fist %.0f (difference %.0f)\n", f, calNew.open[f],
                      calNew.fist[f], d);
        if (d < 150) ok = false;
      }
      cal = CAL_IDLE;
      if (ok) {
        P = calNew;
        prefs.putBytes("p", &P, sizeof(P));
        buzz.play(600, 100, 1, now);
        Serial.printf("CAL: saved. neutral pitch %.1f roll %.1f\n", P.pitch0, P.roll0);
      } else {
        buzz.play(80, 120, 3, now);
        Serial.println("CAL failed: a finger changed less than 150 (sensor loose / not bending?). Try again.");
      }
      return;
  }
}

// ---------------------------------------------------------------- serial
static char line[96];
static int lineLen = 0;

static void command(char* s) {
  char* c = strtok(s, " \t\r\n");
  if (!c) return;
  char* a1 = strtok(nullptr, " \t\r\n");
  char* a2 = strtok(nullptr, " \t\r\n");
  const float v1 = a1 ? atof(a1) : 0, v2 = a2 ? atof(a2) : 0;
  for (char* p = c; *p; ++p) *p = toupper(*p);
  if (!strcmp(c, "SHOW")) {
    live = !live;
  } else if (!strcmp(c, "CAL")) {
    calStart(millis());
  } else if (!strcmp(c, "THUMB") && a1) {
    P.thumb = v1 != 0;
    Serial.println("ok");
  } else if (!strcmp(c, "AXIS") && a1 && a2 && fabsf(v1) == 1 && fabsf(v2) == 1) {
    P.pitch_sign = (int8_t)v1;
    P.roll_sign = (int8_t)v2;
    Serial.println("ok");
  } else if (!strcmp(c, "LINK") && a1 && v1 >= 0 && v1 <= 255) {
    P.link_id = (uint8_t)v1;
    Serial.println("ok (same number on the robot: LINK, SAVE)");
  } else if (!strcmp(c, "DEAD") && a1 && v1 >= 0 && v1 < P.full) {
    P.dead = v1;
    Serial.println("ok");
  } else if (!strcmp(c, "FULL") && a1 && v1 > P.dead && v1 <= 80) {
    P.full = v1;
    Serial.println("ok");
  } else if (!strcmp(c, "SAVE")) {
    Serial.println(prefs.putBytes("p", &P, sizeof(P)) == sizeof(P) ? "ok" : "error: save failed");
  } else if (!strcmp(c, "DEFAULTS")) {
    gloveDefaults(P);
    Serial.println("ok (CAL next)");
  } else {
    Serial.println("SHOW | CAL | THUMB 0|1 | AXIS pitch_sign roll_sign (1/-1) | LINK id | DEAD deg | FULL deg | SAVE | DEFAULTS");
  }
}

static void pollSerial() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (!lineLen) continue;
      line[lineLen] = 0;
      lineLen = 0;
      command(line);
    } else if (lineLen < (int)sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
}

// ---------------------------------------------------------------- main
void setup() {
  Serial.begin(SERIAL_BAUD);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_VIBRATE, OUTPUT);
  digitalWrite(PIN_VIBRATE, LOW);
  pinMode(PIN_LED, OUTPUT);
  led(false);
  analogReadResolution(12);
  for (int f = 0; f < NF; ++f) analogSetPinAttenuation(PIN_FLEX[f], ADC_11db);

  prefs.begin("glove", false);
  gloveDefaults(P);
  GloveParams t;
  if (prefs.getBytes("p", &t, sizeof(t)) == sizeof(t) && gloveValid(t)) P = t;

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  imuOk = mpuBegin();
  for (int f = 0; f < NF; ++f) flexRaw[f] = analogRead(PIN_FLEX[f]);
  if (!radioBegin()) Serial.println("ESP-NOW init failed");
  Serial.printf("glove ready | link %d | MPU6050 %s | hold the button 2 s to calibrate\n", P.link_id,
                imuOk ? "ok" : "NOT FOUND");
  buzz.play(60, 80, imuOk ? 1 : 5, millis());
}

void loop() {
  static uint32_t nextUs = micros();
  static uint32_t lastSend = 0, lastLive = 0, nextWarn = 0;
  static uint32_t btnDown = 0;
  static bool btnWas = false, btnLong = false;
  static uint8_t seq = 0;
  static uint8_t lastActive = G_NONE;
  static GlovePacket pkt;

  pollSerial();
  if ((int32_t)(micros() - nextUs) < 0) return;
  nextUs += SAMPLE_US;
  if ((int32_t)(micros() - nextUs) > 100000) nextUs = micros();
  const uint32_t now = millis();

  // sensors
  for (int f = 0; f < NF; ++f) flexRaw[f] += 0.3f * (analogRead(PIN_FLEX[f]) - flexRaw[f]);
  if (imuOk && mpuRead()) tilt.update(ax, ay, az, gx, gy, SAMPLE_US / 1e6f);
  for (int f = 0; f < NF; ++f) bend[f] = bendOf(flexRaw[f], P.open[f], P.fist[f]);
  fingers.update(bend, P);
  const bool eventFired = filter.update(classify(fingers.straight), now);

  // button (active low)
  const bool btn = digitalRead(PIN_BUTTON) == LOW;
  if (btn && !btnWas) {
    btnDown = now;
    btnLong = false;
  }
  if (btn && !btnLong && now - btnDown > 2000 && cal == CAL_IDLE) {
    btnLong = true;
    calStart(now);
  }
  if (!btn && btnWas && !btnLong && now - btnDown > 30 && cal == CAL_IDLE) {
    armed = !armed && imuOk;
    if (armed) buzz.play(150, 50, 1, now);
    else buzz.play(60, 80, 2, now);
    Serial.println(armed ? "ARMED" : "disarmed");
  }
  btnWas = btn;
  calUpdate(now);

  // feedback
  if (armed && !buzz.busy()) {
    if (eventFired) buzz.play(250, 50, 1, now);
    else if (filter.active != lastActive) buzz.play(30, 30, 1, now);
  }
  lastActive = filter.active;
  if (linkOk(now)) everLinked = true;
  if (armed && (int32_t)(now - nextWarn) >= 0 && !buzz.busy()) {
    if (!linkOk(now)) {
      buzz.play(50, 80, 3, now);            // robot not answering: every 2 s
      nextWarn = now + 2000;
    } else if (statusFlags & SF_LOW_BATTERY) {
      buzz.play(400, 100, 1, now);          // every 5 s
      nextWarn = now + 5000;
    } else if (statusFlags & SF_OBSTACLE) {
      buzz.play(40, 60, 2, now);            // every 0.5 s
      nextWarn = now + 500;
    }
  }
  buzz.update(now);

  // LED: on = armed, slow blink = disarmed, fast blink = calibrating
  if (cal != CAL_IDLE) led((now / 100) % 2);
  else if (armed) led(true);
  else led((now / 1000) % 2 && (now % 1000) < 100);

  // packet
  if (now - lastSend >= SEND_MS) {
    lastSend = now;
    const float pr = P.pitch_sign * (tilt.pitch - P.pitch0);
    const float rr = P.roll_sign * (tilt.roll - P.roll0);
    const bool on = armed && cal == CAL_IDLE;
    makeCommand(on ? gestureMode(filter.active) : (uint8_t)MODE_STOP, pr, rr, P, pkt);
    pkt.flags = on ? GF_ARMED : 0;
    pkt.event = filter.event;
    pkt.event_seq = filter.eventSeq;
    seal(pkt, PKT_GLOVE, P.link_id, seq++);
    esp_now_send(kBroadcast, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
  }

  if (live && now - lastLive >= 200) {
    lastLive = now;
    Serial.printf("raw %4.0f %4.0f %4.0f %4.0f %4.0f | bend %.2f %.2f %.2f %.2f %.2f | %-7s | "
                  "pitch %5.1f roll %5.1f | x %4d y %4d turn %4d | %s | robot %s %.2f V\n",
                  flexRaw[0], flexRaw[1], flexRaw[2], flexRaw[3], flexRaw[4], bend[0], bend[1], bend[2],
                  bend[3], bend[4], gestureName(filter.active), tilt.pitch - P.pitch0, tilt.roll - P.roll0,
                  pkt.x, pkt.y, pkt.turn, armed ? "ARMED" : "disarmed",
                  linkOk(now) ? "ok" : (everLinked ? "LOST" : "--"), statusMv / 1000.0f);
  }
}

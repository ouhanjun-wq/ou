// 18-servo hexapod (3 servos per leg) on the ESP32 DevKit of the kit, controlled by the gesture
// glove over ESP-NOW, or by text commands on the USB serial port (115200 baud).
//
// Board: "ESP32 Dev Module". No extra libraries.
// Servos: through the kit's servo board, PCA9685 (I2C) or direct GPIO, see servo_out.h.
//
// Glove gestures (the glove sends a mode, the robot does the walking):
//   open hand   walk: tilt forward / back = forward / back, tilt left / right = turn
//   index only  crab: tilt forward / back = forward / back, tilt left / right = sideways
//   victory     body: the body tilts like your hand, feet stay put
//   fist        stop                     thumb up (1 s)  stand up / sit down
//   rock (index + little finger, 0.6 s)  next gait: tripod -> ripple -> wave
// No glove packet for 0.3 s: the robot stops walking and stands.
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "commands.h"
#include "config.h"
#include "params.h"
#include "protocol.h"
#include "robot.h"
#include "servo_out.h"

using namespace hx;

static Params P;
static Robot R;
static Preferences prefs;
static uint8_t backend = BACKEND_PCA9685;

// ---------------------------------------------------------------- radio
static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
static GlovePacket gRx;
static volatile bool gRxNew = false;
static volatile uint32_t gRxCount = 0;
static uint8_t gTxSeq = 0;

static void handleRx(const uint8_t* data, int len) {
  GlovePacket g;
  if (!hx::open(data, len, PKT_GLOVE, P.link_id, g)) return;
  portENTER_CRITICAL(&gMux);
  gRx = g;
  gRxNew = true;
  portEXIT_CRITICAL(&gMux);
  gRxCount = gRxCount + 1;
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

// ---------------------------------------------------------------- storage, serial
static bool saveParams(const Params& p) {
  return prefs.putBytes("p", &p, sizeof(p)) == sizeof(p);
}

static void loadParams() {
  paramsDefaults(P);
  Params t;
  if (prefs.getBytes("p", &t, sizeof(t)) == sizeof(t) && paramsValid(t)) P = t;
}

class SerialOut : public Out {
 public:
  void write(const char* s) override { Serial.print(s); }
};
static SerialOut out;

static void scanI2C(Out& o) {
  int found = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      o.printf("I2C device at 0x%02X%s\n", a,
               a == 0x40 || a == 0x41 ? " (PCA9685)" : a == 0x70 ? " (PCA9685 all-call)" : "");
      ++found;
    }
  }
  if (!found) o.printf("no I2C device on SDA %d / SCL %d: GPIO servo board? (BACKEND GPIO)\n", PIN_SDA, PIN_SCL);
}

static char line[160];
static int lineLen = 0;

static void pollSerial() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen == 0) continue;
      line[lineLen] = 0;
      lineLen = 0;
      Hooks h;
      h.save = saveParams;
      h.scan = scanI2C;
      runCommand(line, R, P, h, out, millis());
    } else if (lineLen < (int)sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
}

// ---------------------------------------------------------------- sensors
static float vbatFilt = 0;

static void readBattery() {
  if (PIN_VBAT < 0) return;
  const float v = analogReadMilliVolts(PIN_VBAT) * P.vdiv;
  if (v < 1500) {                  // nothing connected
    vbatFilt = 0;
    R.vbatMv = 0;
    return;
  }
  vbatFilt = vbatFilt < 1 ? v : vbatFilt + 0.2f * (v - vbatFilt);
  R.vbatMv = (uint16_t)vbatFilt;
}

static void readDistance() {
  if (PIN_TRIG < 0 || PIN_ECHO < 0) return;
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  const unsigned long t = pulseIn(PIN_ECHO, HIGH, 12000);   // up to ~2 m
  R.distMm = t ? (uint16_t)(t * 0.1715f) : 0;
}

static bool ledIsServo() {
  if (backend != BACKEND_GPIO) return false;
  for (int s = 0; s < NSERVO; ++s)
    if (P.ch[s] == PIN_LED) return true;
  return false;
}

// ---------------------------------------------------------------- main
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);
  prefs.begin("hexapod", false);
  loadParams();
  R.begin(&P);

  backend = servo::begin(P, PIN_SDA, PIN_SCL);
  for (int s = 0; s < NSERVO; ++s) servo::write(s, 0);
  if (backend == BACKEND_PCA9685) {
    Serial.printf("servos: PCA9685 at 0x40%s\n", servo::board41() ? " and 0x41" : " (no 0x41)");
    bool need41 = false;
    for (int s = 0; s < NSERVO; ++s) need41 |= P.ch[s] >= 16;
    if (need41 && !servo::board41())
      Serial.println("warning: CHMAP uses channels 16..31 but there is no PCA9685 at 0x41 (see SCAN)");
  } else {
    Serial.println("servos: direct GPIO (CHMAP = GPIO numbers)");
  }

  if (PIN_TRIG >= 0 && PIN_ECHO >= 0) {
    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
  }
  if (!ledIsServo()) pinMode(PIN_LED, OUTPUT);

  if (!radioBegin()) Serial.println("ESP-NOW init failed");
  Serial.printf("hexapod ready | link %d | MAC %s | HELP for commands\n", P.link_id,
                WiFi.macAddress().c_str());
}

void loop() {
  static uint32_t nextUs = micros();
  static uint32_t lastStatus = 0, lastSense = 0, lastLog = 0;
  static uint8_t lastState = 255;

  pollSerial();

  if ((int32_t)(micros() - nextUs) < 0) return;
  nextUs += LOOP_US;
  if ((int32_t)(micros() - nextUs) > 100000) nextUs = micros();   // fell far behind: resync
  const uint32_t now = millis();

  if (gRxNew) {
    GlovePacket g;
    portENTER_CRITICAL(&gMux);
    g = gRx;
    gRxNew = false;
    portEXIT_CRITICAL(&gMux);
    R.onGlove(g, now);
  }

  if (now - lastSense >= 100) {
    lastSense = now;
    readBattery();
    readDistance();
  }

  R.update(LOOP_US / 1e6f, now);
  for (int s = 0; s < NSERVO; ++s) servo::write(s, R.servosOn ? (uint16_t)R.us[s] : 0);

  if (now - lastStatus >= STATUS_MS) {
    lastStatus = now;
    StatusPacket st = R.status();
    seal(st, PKT_STATUS, P.link_id, gTxSeq++);
    esp_now_send(kBroadcast, reinterpret_cast<const uint8_t*>(&st), sizeof(st));
  }

  if (R.state != lastState) {
    static const char* names[] = {"RELAXED", "STANDING_UP", "READY", "SITTING_DOWN"};
    Serial.printf("state %s%s\n", names[R.state & 3], R.lowBattery ? " (battery low)" : "");
    lastState = R.state;
  }
  if (now - lastLog >= 5000) {
    lastLog = now;
    Serial.printf("link %s (%lu packets) | %.2f V\n", R.linkOk(now) ? "ok" : "--",
                  (unsigned long)gRxCount, R.vbatMv / 1000.0f);
  }

  // LED: off = relaxed, on = standing, fast blink = no glove, slow blink = battery low
  if (!ledIsServo()) {
    bool on = R.state != RS_RELAXED;
    if (R.lowBattery) on = (now / 500) % 2;
    else if (R.state != RS_RELAXED && !R.linkOk(now)) on = (now / 100) % 2;
    digitalWrite(PIN_LED, on ? HIGH : LOW);
  }
}

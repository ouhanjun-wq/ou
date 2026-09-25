// Butterfly ground station / remote control
// Board: Seeed Studio XIAO ESP32S3 (Arduino-ESP32 core 3.x, "USB CDC On Boot: Enabled")
//
// Inputs:  throttle potentiometer, 2 joysticks (roll/pitch + yaw), ARM switch, MODE switch
// Text commands (voice / AI / PC hook) on USB Serial and on Serial1 RX (D7):
//   ARM | DISARM | MODE MANUAL|STAB|HOLD | TAKEOFF | LAND | UP | DOWN | THR <0..1>
//   LEFT [deg] | RIGHT [deg] | TURN <deg> | STICKS | SET <param> <value> | SAVE | CALIB
//   TEL ON|OFF | STATUS
// Moving any stick takes control back from text commands immediately (human override).
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "protocol.h"

// ---------------- configuration ----------------
constexpr uint8_t NET_ID = 1;             // must match butterfly param net_id
constexpr bool HAS_STICKS = true;         // false: no joysticks wired, text commands only

constexpr int PIN_THR   = D0;             // GPIO1  throttle pot (ADC1)
constexpr int PIN_ROLL  = D1;             // GPIO2  right stick X
constexpr int PIN_PITCH = D2;             // GPIO3  right stick Y
constexpr int PIN_YAW   = D3;             // GPIO4  left stick X
constexpr int PIN_ARM   = D4;             // GPIO5  switch to GND = ARM
constexpr int PIN_MODE  = D5;             // GPIO6  switch to GND = STABILIZE
constexpr int PIN_VOICE_RX = D7;          // GPIO44 Serial1 RX: voice module TX
constexpr uint32_t VOICE_BAUD = 115200;
constexpr int PIN_LED = LED_BUILTIN;

// Flip an axis if it moves the wrong way on the status screen.
constexpr bool INV_THR = false, INV_ROLL = false, INV_PITCH = true, INV_YAW = false;

constexpr float TAKEOFF_THR = 0.75f;      // throttle target for TAKEOFF
constexpr float THR_SLEW = 0.4f;          // text-command throttle ramp (per second)
constexpr float DEADBAND = 0.04f;
constexpr float OVERRIDE = 0.25f;         // stick deflection that cancels text control

// ---------------- state ----------------
static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t oneShotSeq = 0, ctrlSeq = 0;
static int centerRoll = 2048, centerPitch = 2048, centerYaw = 2048;

enum Source { SRC_STICKS, SRC_TEXT };
static Source source = SRC_STICKS;
static bool softArm = true;
static int modeOverride = -1;             // -1: follow MODE switch
static bool lastModeSwitch = false;
static float vThr = 0, vThrTarget = 0, thrAtHandover = 0;
static bool telemetryPrint = false;

static portMUX_TYPE telMux = portMUX_INITIALIZER_UNLOCKED;
static proto::TelemetryPacket lastTel;
static uint32_t lastTelMs = 0;
static volatile bool telFresh = false;

// ---------------- radio ----------------
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
  proto::TelemetryPacket t;
  if (!proto::open(data, len, proto::PKT_TELEMETRY, NET_ID, t)) return;
  portENTER_CRITICAL(&telMux);
  lastTel = t;
  lastTelMs = millis();
  portEXIT_CRITICAL(&telMux);
  telFresh = true;
}

static bool radioBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(proto::WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcast, 6);
  peer.channel = proto::WIFI_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

template <class T>
static void sendOneShot(T& pkt, uint8_t type) {
  proto::seal(pkt, type, NET_ID, oneShotSeq++);
  for (int i = 0; i < 3; ++i) {     // repeated for reliability; receiver de-duplicates by seq
    esp_now_send(kBroadcast, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    delay(3);
  }
}

static void sendCommand(uint8_t cmd, int16_t arg = 0) {
  proto::CommandPacket c = {};
  c.cmd = cmd;
  c.arg = arg;
  sendOneShot(c, proto::PKT_COMMAND);
}

static void sendParam(const char* name, float value) {
  proto::ParamPacket p = {};
  strncpy(p.name, name, sizeof(p.name) - 1);
  p.value = value;
  sendOneShot(p, proto::PKT_PARAM);
}

// ---------------- sticks ----------------
static float axis(int pin, int center, bool invert) {
  float v = (analogRead(pin) - center) / 2048.0f;
  if (fabsf(v) < DEADBAND) v = 0;
  v = constrain(v, -1.0f, 1.0f);
  return invert ? -v : v;
}

static void readSticks(float& thr, float& roll, float& pitch, float& yaw) {
  if (!HAS_STICKS) { thr = roll = pitch = yaw = 0; return; }
  thr = constrain(analogRead(PIN_THR) / 4095.0f, 0.0f, 1.0f);
  if (INV_THR) thr = 1.0f - thr;
  if (thr < 0.02f) thr = 0;
  roll = axis(PIN_ROLL, centerRoll, INV_ROLL);
  pitch = axis(PIN_PITCH, centerPitch, INV_PITCH);
  yaw = axis(PIN_YAW, centerYaw, INV_YAW);
}

static int averageRead(int pin) {
  long sum = 0;
  for (int i = 0; i < 32; ++i) { sum += analogRead(pin); delay(2); }
  return (int)(sum / 32);
}

// ---------------- text commands ----------------
static const char* modeName(int m) {
  return m == proto::MODE_MANUAL ? "MANUAL" : m == proto::MODE_STABILIZE ? "STAB" : m == proto::MODE_HEADING_HOLD ? "HOLD" : "?";
}

static void takeTextControl(Stream& out) {
  if (source == SRC_TEXT) return;
  float thr, r, p, y;
  readSticks(thr, r, p, y);
  vThr = vThrTarget = thr;
  thrAtHandover = thr;
  source = SRC_TEXT;
  out.println("text control ON (move a stick to take over)");
}

static void printStatus(Stream& out) {
  proto::TelemetryPacket t;
  uint32_t age;
  portENTER_CRITICAL(&telMux);
  t = lastTel;
  age = millis() - lastTelMs;
  portEXIT_CRITICAL(&telMux);
  float thr, r, p, y;
  readSticks(thr, r, p, y);
  out.printf("source %s | arm switch %s softArm %d | mode %s | sticks thr %.2f roll %+.2f pitch %+.2f yaw %+.2f | vThr %.2f\n",
             source == SRC_TEXT ? "TEXT" : "STICKS", digitalRead(PIN_ARM) == LOW ? "ON" : "OFF", softArm,
             modeName(modeOverride >= 0 ? modeOverride : (digitalRead(PIN_MODE) == LOW ? 1 : 0)),
             thr, r, p, y, vThr);
  if (lastTelMs == 0 || age > 1000) {
    out.println("butterfly: NO TELEMETRY");
  } else {
    out.printf("butterfly: state %u mode %s roll %.1f pitch %.1f yaw %.1f | vbat %.2f V | flap %.1f Hz | link %u pkt/s | flags 0x%02X\n",
               t.state, modeName(t.mode), t.roll_cd / 100.0f, t.pitch_cd / 100.0f, t.yaw_cd / 100.0f,
               t.vbat_mv / 1000.0f, t.flap_dhz / 10.0f, t.link_pps, t.flags);
  }
}

static void handleLine(char* line, Stream& out) {
  char* argv[4];
  int argc = 0;
  for (char* tok = strtok(line, " \t"); tok && argc < 4; tok = strtok(nullptr, " \t")) argv[argc++] = tok;
  if (argc == 0) return;
  for (char* c = argv[0]; *c; ++c) *c = toupper(*c);
  const char* cmd = argv[0];
  auto arg1Upper = [&]() { for (char* c = argv[1]; *c; ++c) *c = toupper(*c); return argv[1]; };

  if (!strcmp(cmd, "ARM")) { softArm = true; out.println("soft arm ON (arm switch must also be ON)"); }
  else if (!strcmp(cmd, "DISARM")) { softArm = false; out.println("DISARMED"); }
  else if (!strcmp(cmd, "MODE") && argc == 2) {
    const char* m = arg1Upper();
    if (!strcmp(m, "MANUAL")) modeOverride = proto::MODE_MANUAL;
    else if (!strcmp(m, "STAB")) modeOverride = proto::MODE_STABILIZE;
    else if (!strcmp(m, "HOLD")) modeOverride = proto::MODE_HEADING_HOLD;
    else { out.println("MODE MANUAL|STAB|HOLD"); return; }
    out.printf("mode %s (flip MODE switch to return to switch control)\n", m);
  } else if (!strcmp(cmd, "TAKEOFF")) {
    takeTextControl(out);
    if (modeOverride < 0) modeOverride = proto::MODE_HEADING_HOLD;
    vThrTarget = TAKEOFF_THR;
    out.printf("TAKEOFF: throttle -> %.2f, mode %s\n", vThrTarget, modeName(modeOverride));
  } else if (!strcmp(cmd, "LAND")) {
    takeTextControl(out);
    vThrTarget = 0;
    out.println("LAND: throttle -> 0 (glide down)");
  } else if (!strcmp(cmd, "UP") || !strcmp(cmd, "DOWN")) {
    takeTextControl(out);
    vThrTarget = constrain(vThrTarget + (!strcmp(cmd, "UP") ? 0.1f : -0.1f), 0.0f, 1.0f);
    out.printf("throttle -> %.2f\n", vThrTarget);
  } else if (!strcmp(cmd, "THR") && argc == 2) {
    takeTextControl(out);
    vThrTarget = constrain(atof(argv[1]), 0.0, 1.0);
    out.printf("throttle -> %.2f\n", vThrTarget);
  } else if (!strcmp(cmd, "LEFT") || !strcmp(cmd, "RIGHT") || (!strcmp(cmd, "TURN") && argc == 2)) {
    int deg = argc == 2 ? atoi(argv[1]) : 30;
    if (!strcmp(cmd, "LEFT")) deg = -abs(deg);
    if (!strcmp(cmd, "RIGHT")) deg = abs(deg);
    sendCommand(proto::CMD_TURN, (int16_t)constrain(deg, -180, 180));
    out.printf("turn %+d deg (works in HOLD mode)\n", deg);
  } else if (!strcmp(cmd, "STICKS")) {
    source = SRC_STICKS;
    out.println("stick control");
  } else if (!strcmp(cmd, "SET") && argc == 3) {
    for (char* c = argv[1]; *c; ++c) *c = tolower(*c);
    sendParam(argv[1], atof(argv[2]));
    out.printf("sent %s = %s\n", argv[1], argv[2]);
  } else if (!strcmp(cmd, "SAVE")) {
    sendCommand(proto::CMD_SAVE);
    out.println("save sent (butterfly must be disarmed)");
  } else if (!strcmp(cmd, "CALIB")) {
    sendCommand(proto::CMD_CALIB_GYRO);
    out.println("gyro calibration sent (keep butterfly still)");
  } else if (!strcmp(cmd, "TEL") && argc == 2) {
    telemetryPrint = !strcmp(arg1Upper(), "ON");
    if (telemetryPrint) out.println("# TEL,ms,roll,pitch,yaw,ur,up,uy,vbat,mode,state,flags,flap_hz,link_pps");
  } else if (!strcmp(cmd, "STATUS")) {
    printStatus(out);
  } else {
    out.println("? ARM DISARM MODE TAKEOFF LAND UP DOWN THR LEFT RIGHT TURN STICKS SET SAVE CALIB TEL STATUS");
  }
}

static void pollStream(Stream& s, char* buf, size_t& len, size_t cap) {
  while (s.available()) {
    const char c = (char)s.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[len] = '\0'; handleLine(buf, Serial); len = 0; }   // replies go to USB
    else if (len < cap - 1) buf[len++] = c;
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  Serial1.begin(VOICE_BAUD, SERIAL_8N1, PIN_VOICE_RX, -1);
  pinMode(PIN_ARM, INPUT_PULLUP);
  pinMode(PIN_MODE, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  analogReadResolution(12);
  delay(300);

  if (HAS_STICKS) {   // sticks must be centred at power-up
    centerRoll = averageRead(PIN_ROLL);
    centerPitch = averageRead(PIN_PITCH);
    centerYaw = averageRead(PIN_YAW);
  }
  lastModeSwitch = digitalRead(PIN_MODE) == LOW;
  oneShotSeq = (uint8_t)random(256);   // avoid seq collisions after a ground-station reboot
  Serial.println(radioBegin() ? "ground station ready (ESP-NOW)" : "ESP-NOW init FAILED");
  Serial.println("type STATUS");
}

void loop() {
  static uint32_t lastSend = 0, lastTelPrint = 0;
  static char usbBuf[96], voiceBuf[96];
  static size_t usbLen = 0, voiceLen = 0;

  pollStream(Serial, usbBuf, usbLen, sizeof(usbBuf));
  pollStream(Serial1, voiceBuf, voiceLen, sizeof(voiceBuf));

  const uint32_t now = millis();
  if (now - lastSend >= 20) {   // 50 Hz
    const float dt = (now - lastSend) / 1000.0f;
    lastSend = now;

    float thr, roll, pitch, yaw;
    readSticks(thr, roll, pitch, yaw);
    const bool modeSwitch = digitalRead(PIN_MODE) == LOW;
    if (modeSwitch != lastModeSwitch) { modeOverride = -1; lastModeSwitch = modeSwitch; }

    if (source == SRC_TEXT && HAS_STICKS &&
        (fabsf(roll) > OVERRIDE || fabsf(pitch) > OVERRIDE || fabsf(yaw) > OVERRIDE ||
         fabsf(thr - thrAtHandover) > 0.1f)) {
      source = SRC_STICKS;
      Serial.println("manual override: stick control");
    }
    if (source == SRC_TEXT) {
      const float step = THR_SLEW * dt;
      vThr += constrain(vThrTarget - vThr, -step, step);
      thr = vThr;
      roll = pitch = yaw = 0;
    }

    proto::ControlPacket c = {};
    c.thr = (int16_t)lroundf(thr * 1000);
    c.roll = (int16_t)lroundf(roll * 500);
    c.pitch = (int16_t)lroundf(pitch * 500);
    c.yaw = (int16_t)lroundf(yaw * 500);
    c.mode = modeOverride >= 0 ? modeOverride : (modeSwitch ? proto::MODE_STABILIZE : proto::MODE_MANUAL);
    const bool armSwitch = HAS_STICKS ? digitalRead(PIN_ARM) == LOW : true;
    c.armed = (armSwitch && softArm) ? 1 : 0;
    proto::seal(c, proto::PKT_CONTROL, NET_ID, ctrlSeq++);
    esp_now_send(kBroadcast, reinterpret_cast<const uint8_t*>(&c), sizeof(c));
  }

  if (telemetryPrint && telFresh && now - lastTelPrint >= 50) {
    telFresh = false;
    lastTelPrint = now;
    proto::TelemetryPacket t;
    portENTER_CRITICAL(&telMux);
    t = lastTel;
    portEXIT_CRITICAL(&telMux);
    Serial.printf("TEL,%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%u,%u,%.1f,%u\n", (unsigned long)t.ms,
                  t.roll_cd / 100.0f, t.pitch_cd / 100.0f, t.yaw_cd / 100.0f, t.ur_cd / 100.0f,
                  t.up_cd / 100.0f, t.uy_cd / 100.0f, t.vbat_mv / 1000.0f, t.mode, t.state, t.flags,
                  t.flap_dhz / 10.0f, t.link_pps);
  }

  // LED: on while telemetry is arriving, blinking when the butterfly is silent
  const bool linked = lastTelMs != 0 && now - lastTelMs < 500;
  digitalWrite(PIN_LED, (linked || (now / 250) % 2) ? LOW : HIGH);
  delay(1);
}

// Butterfly ground station: Xbox controller (BLE) or DIY sticks -> ESP-NOW -> butterfly
// Board: Seeed Studio XIAO ESP32S3 (Arduino-ESP32 core 3.x, "USB CDC On Boot: Enabled")
//
// Xbox Wireless Controller (Series X|S, or One with BLE firmware 5.x+):
//   Left stick  up/down    AUTO: climb / descend (release = hold altitude)
//   Left stick  left/right yaw (turn on the spot / change heading)
//   Right stick up/down    push = nose down = faster, pull = nose up = slower
//   Right stick left/right bank left / right
//   RT                     throttle in MANUAL / STAB / HOLD
//   A (hold 1 s) arm       B disarm
//   D-pad  up AUTO | right HOLD | down STAB | left MANUAL
//   Y  U-turn 180 deg      LB / RB  turn 45 deg left / right (HOLD, AUTO)
//   View  gyro calibration (disarmed)
//
// Text commands (voice / AI / PC) on USB Serial and Serial1 RX (D7), one per line:
//   ARM | DISARM | MODE MANUAL|STAB|HOLD|AUTO | TAKEOFF | LAND | UP | DOWN | THR <0..1>
//   LEFT [deg] | RIGHT [deg] | TURN <deg> | STICKS | SET <param> <value> | SAVE | CALIB
//   TEL ON|OFF | STATUS
// Moving a stick takes control back from text commands immediately (human override).
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "protocol.h"

// ---------------- configuration ----------------
#ifndef USE_XBOX
#define USE_XBOX 1                        // 1: Xbox controller over BLE, 0: DIY sticks + switches
#endif

constexpr uint8_t NET_ID = 1;             // must match butterfly param net_id
constexpr int PIN_VOICE_RX = D7;          // GPIO44 Serial1 RX: voice module TX
constexpr uint32_t VOICE_BAUD = 115200;
constexpr int PIN_LED = LED_BUILTIN;

constexpr float TAKEOFF_THR = 0.75f;      // text TAKEOFF throttle in non-AUTO modes
constexpr float THR_SLEW = 0.4f;          // text-command throttle ramp (per second)
constexpr float OVERRIDE = 0.25f;         // stick deflection that cancels text control

#if USE_XBOX
#include <XboxSeriesXControllerESP32_asukiaaa.hpp>
XboxSeriesXControllerESP32_asukiaaa::Core xbox;   // pairs with the first Xbox controller found
constexpr float DEADBAND = 0.08f;
constexpr uint32_t ARM_HOLD_MS = 1000;
#else
constexpr bool HAS_STICKS = true;         // false: no sticks wired, text commands only
constexpr int PIN_THR   = D0;             // GPIO1  throttle pot (ADC1)
constexpr int PIN_ROLL  = D1;             // GPIO2  right stick X
constexpr int PIN_PITCH = D2;             // GPIO3  right stick Y
constexpr int PIN_YAW   = D3;             // GPIO4  left stick X
constexpr int PIN_ARM   = D4;             // GPIO5  switch to GND = ARM
constexpr int PIN_MODE  = D5;             // GPIO6  switch to GND = STABILIZE
constexpr bool INV_THR = false, INV_ROLL = false, INV_PITCH = true, INV_YAW = false;
constexpr float DEADBAND = 0.04f;
#endif

// ---------------- state ----------------
static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t oneShotSeq = 0, ctrlSeq = 0;

// Normalised pilot input from whichever device is compiled in.
struct Pilot {
  bool valid = false;               // device connected and delivering data
  float thr = 0;                    // 0..1 (MANUAL / STAB / HOLD)
  float climb = 0;                  // -1..1 (AUTO), + = up
  float roll = 0, pitch = 0, yaw = 0;  // -1..1, pitch + = nose up
};

enum Source { SRC_STICKS, SRC_TEXT };
static Source source = SRC_STICKS;
static uint8_t mode = proto::MODE_MANUAL;
static bool armed = false;               // Xbox: A/B buttons.  DIY: ARM switch && softArm
static bool softArm = true;
static float vThr = 0, vThrTarget = 0, textClimb = 0;
static uint32_t textClimbUntil = 0;
static Pilot handover;                   // stick positions when text control started
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

// Not a template: the .ino preprocessor generates broken prototypes for templates.
static void sendRepeated(const void* pkt, size_t len) {
  for (int i = 0; i < 3; ++i) {     // repeated for reliability; receiver de-duplicates by seq
    esp_now_send(kBroadcast, static_cast<const uint8_t*>(pkt), len);
    delay(3);
  }
}

static void sendCommand(uint8_t cmd, int16_t arg) {
  proto::CommandPacket c = {};
  c.cmd = cmd;
  c.arg = arg;
  proto::seal(c, proto::PKT_COMMAND, NET_ID, oneShotSeq++);
  sendRepeated(&c, sizeof(c));
}

static void sendParam(const char* name, float value) {
  proto::ParamPacket p = {};
  strncpy(p.name, name, sizeof(p.name) - 1);
  p.value = value;
  proto::seal(p, proto::PKT_PARAM, NET_ID, oneShotSeq++);
  sendRepeated(&p, sizeof(p));
}

static bool butterflyDisarmed() {
  portENTER_CRITICAL(&telMux);
  const bool d = lastTel.state == proto::ST_DISARMED;
  portEXIT_CRITICAL(&telMux);
  return d;
}

static const char* modeName(int m) {
  switch (m) {
    case proto::MODE_MANUAL: return "MANUAL";
    case proto::MODE_STABILIZE: return "STAB";
    case proto::MODE_HEADING_HOLD: return "HOLD";
    case proto::MODE_AUTO: return "AUTO";
    default: return "?";
  }
}

static float deadband(float v) {
  if (fabsf(v) < DEADBAND) return 0;
  return constrain((v - (v > 0 ? DEADBAND : -DEADBAND)) / (1.0f - DEADBAND), -1.0f, 1.0f);
}

// ---------------- input: Xbox controller ----------------
#if USE_XBOX
static void readPilot(Pilot& p) {
  p = Pilot();
  if (!xbox.isConnected() || xbox.isWaitingForFirstNotification()) return;
  const auto& n = xbox.xboxNotif;
  const float jm = (float)XboxControllerNotificationParser::maxJoy / 2.0f;
  const float tm = 1023.0f;   // triggers report 0 .. 0x3ff
  const float lx = deadband((n.joyLHori - jm) / jm), ly = deadband((n.joyLVert - jm) / jm);  // up = -1
  const float rx = deadband((n.joyRHori - jm) / jm), ry = deadband((n.joyRVert - jm) / jm);
  p.valid = true;
  p.yaw = lx;
  p.climb = -ly;
  p.roll = rx;
  p.pitch = ry;             // pull back (+) = nose up
  p.thr = constrain(n.trigRT / tm, 0.0f, 1.0f);
  if (p.thr < 0.02f) p.thr = 0;
}

static void handleButtons(const Pilot& p) {
  static bool prev[10] = {false};
  static uint32_t aSince = 0;
  const auto& n = xbox.xboxNotif;
  const bool now[10] = {(bool)n.btnA, (bool)n.btnB, (bool)n.btnY, (bool)n.btnLB, (bool)n.btnRB,
                        (bool)n.btnDirUp, (bool)n.btnDirRight, (bool)n.btnDirDown,
                        (bool)n.btnDirLeft, (bool)n.btnSelect};
  auto pressed = [&](int i) { return now[i] && !prev[i]; };

  // A held for 1 s arms, only with the throttle channel in its safe position.
  static bool armHandled = false;
  if (now[0]) {
    if (aSince == 0) { aSince = millis(); armHandled = false; }
    if (!armed && !armHandled && millis() - aSince >= ARM_HOLD_MS) {
      armHandled = true;
      const bool thrSafe = mode == proto::MODE_AUTO ? p.climb == 0 : p.thr == 0;
      if (thrSafe) armed = true;
      Serial.println(thrSafe ? "ARMED (Xbox A)"
                     : mode == proto::MODE_AUTO ? "arm refused: centre the left stick" : "arm refused: release RT");
    }
  } else {
    aSince = 0;
  }
  if (pressed(1) && armed) { armed = false; Serial.println("DISARMED (Xbox B)"); }
  if (pressed(2)) sendCommand(proto::CMD_TURN, 180);
  if (pressed(3)) sendCommand(proto::CMD_TURN, -45);
  if (pressed(4)) sendCommand(proto::CMD_TURN, 45);
  const uint8_t dpadModes[4] = {proto::MODE_AUTO, proto::MODE_HEADING_HOLD, proto::MODE_STABILIZE,
                                proto::MODE_MANUAL};
  for (int i = 0; i < 4; ++i) {
    if (pressed(5 + i)) {
      mode = dpadModes[i];
      source = SRC_STICKS;
      Serial.printf("mode %s\n", modeName(mode));
    }
  }
  if (pressed(9) && !armed) { sendCommand(proto::CMD_CALIB_GYRO, 0); Serial.println("gyro calibration sent"); }
  memcpy(prev, now, sizeof(prev));
}

// ---------------- input: DIY sticks ----------------
#else
static int centerRoll = 2048, centerPitch = 2048, centerYaw = 2048;
static bool lastModeSwitch = false;

static float axis(int pin, int center, bool invert) {
  const float v = deadband(constrain((analogRead(pin) - center) / 2048.0f, -1.0f, 1.0f));
  return invert ? -v : v;
}

static void readPilot(Pilot& p) {
  p = Pilot();
  if (!HAS_STICKS) return;
  p.valid = true;
  p.thr = constrain(analogRead(PIN_THR) / 4095.0f, 0.0f, 1.0f);
  if (INV_THR) p.thr = 1.0f - p.thr;
  if (p.thr < 0.02f) p.thr = 0;
  p.climb = deadband((p.thr - 0.5f) * 2.0f);   // pot centre = hold altitude in AUTO
  p.roll = axis(PIN_ROLL, centerRoll, INV_ROLL);
  p.pitch = axis(PIN_PITCH, centerPitch, INV_PITCH);
  p.yaw = axis(PIN_YAW, centerYaw, INV_YAW);
}

static void handleButtons(const Pilot& p) {
  (void)p;
  const bool modeSwitch = digitalRead(PIN_MODE) == LOW;
  if (modeSwitch != lastModeSwitch) {
    lastModeSwitch = modeSwitch;
    mode = modeSwitch ? proto::MODE_STABILIZE : proto::MODE_MANUAL;
    source = SRC_STICKS;
  }
  armed = HAS_STICKS ? (digitalRead(PIN_ARM) == LOW && softArm) : softArm;
}

static int averageRead(int pin) {
  long sum = 0;
  for (int i = 0; i < 32; ++i) { sum += analogRead(pin); delay(2); }
  return (int)(sum / 32);
}
#endif

// ---------------- text commands ----------------
static void takeTextControl(const Pilot& p) {
  if (source == SRC_TEXT) return;
  vThr = vThrTarget = p.thr;
  textClimb = 0;
  handover = p;
  source = SRC_TEXT;
  Serial.println("text control ON (move a stick to take over)");
}

static void printStatus() {
  proto::TelemetryPacket t;
  uint32_t age;
  portENTER_CRITICAL(&telMux);
  t = lastTel;
  age = millis() - lastTelMs;
  portEXIT_CRITICAL(&telMux);
  Pilot p;
  readPilot(p);
#if USE_XBOX
  Serial.printf("Xbox %s (battery %d%%) | ", xbox.isConnected() ? "connected" : "searching...",
                xbox.isConnected() ? (int)xbox.battery : 0);
#endif
  Serial.printf("source %s | %s | mode %s | thr %.2f climb %+.2f roll %+.2f pitch %+.2f yaw %+.2f\n",
                source == SRC_TEXT ? "TEXT" : "PILOT", armed ? "ARMED" : "disarmed", modeName(mode),
                p.thr, p.climb, p.roll, p.pitch, p.yaw);
  if (lastTelMs == 0 || age > 1000) {
    Serial.println("butterfly: NO TELEMETRY");
  } else {
    Serial.printf("butterfly: state %u mode %s roll %.1f pitch %.1f yaw %.1f | alt %.2f m vz %+.2f | "
                  "vbat %.2f V | flap %.1f Hz | link %u pkt/s | flags 0x%02X%s\n",
                  t.state, modeName(t.mode), t.roll_cd / 100.0f, t.pitch_cd / 100.0f, t.yaw_cd / 100.0f,
                  t.alt_cm / 100.0f, t.vz_cms / 100.0f, t.vbat_mv / 1000.0f, t.flap_dhz / 10.0f,
                  t.link_pps, t.flags, (t.flags & proto::FLAG_CHARGING) ? " CHARGING (arming locked)" : "");
  }
}

static void handleLine(char* line) {
  char* argv[4];
  int argc = 0;
  for (char* tok = strtok(line, " \t"); tok && argc < 4; tok = strtok(nullptr, " \t")) argv[argc++] = tok;
  if (argc == 0) return;
  for (char* c = argv[0]; *c; ++c) *c = toupper(*c);
  const char* cmd = argv[0];
  Pilot p;
  readPilot(p);

  if (!strcmp(cmd, "ARM")) {
#if USE_XBOX
    armed = true;
    Serial.println("ARMED (text)");
#else
    softArm = true;
    Serial.println("soft arm ON (ARM switch must also be ON)");
#endif
  } else if (!strcmp(cmd, "DISARM")) {
    armed = false;
    softArm = false;
    Serial.println("DISARMED");
  } else if (!strcmp(cmd, "MODE") && argc == 2) {
    for (char* c = argv[1]; *c; ++c) *c = toupper(*c);
    const char* m = argv[1];
    if (!strcmp(m, "MANUAL")) mode = proto::MODE_MANUAL;
    else if (!strcmp(m, "STAB")) mode = proto::MODE_STABILIZE;
    else if (!strcmp(m, "HOLD")) mode = proto::MODE_HEADING_HOLD;
    else if (!strcmp(m, "AUTO")) mode = proto::MODE_AUTO;
    else { Serial.println("MODE MANUAL|STAB|HOLD|AUTO"); return; }
    Serial.printf("mode %s\n", m);
  } else if (!strcmp(cmd, "TAKEOFF")) {
    takeTextControl(p);
    if (mode == proto::MODE_MANUAL || mode == proto::MODE_STABILIZE) mode = proto::MODE_AUTO;
    if (mode == proto::MODE_AUTO) {
      textClimb = 1.0f;                 // launch gesture + climb for 1.5 s, then hold
      textClimbUntil = millis() + 1500;
    } else {
      vThrTarget = TAKEOFF_THR;
    }
    Serial.printf("TAKEOFF in %s\n", modeName(mode));
  } else if (!strcmp(cmd, "LAND")) {
    takeTextControl(p);
    textClimb = -0.7f;
    textClimbUntil = UINT32_MAX;
    vThrTarget = 0;
    Serial.println("LAND: descending / throttle to 0");
  } else if (!strcmp(cmd, "UP") || !strcmp(cmd, "DOWN")) {
    const bool up = !strcmp(cmd, "UP");
    if (mode == proto::MODE_AUTO) {
      sendCommand(proto::CMD_ALT, up ? 100 : -100);
      Serial.printf("altitude target %s 1 m\n", up ? "+" : "-");
    } else {
      takeTextControl(p);
      vThrTarget = constrain(vThrTarget + (up ? 0.1f : -0.1f), 0.0f, 1.0f);
      Serial.printf("throttle -> %.2f\n", vThrTarget);
    }
  } else if (!strcmp(cmd, "THR") && argc == 2) {
    takeTextControl(p);
    vThrTarget = constrain(atof(argv[1]), 0.0, 1.0);
    Serial.printf("throttle -> %.2f (non-AUTO modes)\n", vThrTarget);
  } else if (!strcmp(cmd, "LEFT") || !strcmp(cmd, "RIGHT") || (!strcmp(cmd, "TURN") && argc == 2)) {
    int deg = argc == 2 ? atoi(argv[1]) : 30;
    if (!strcmp(cmd, "LEFT")) deg = -abs(deg);
    if (!strcmp(cmd, "RIGHT")) deg = abs(deg);
    sendCommand(proto::CMD_TURN, (int16_t)constrain(deg, -180, 180));
    Serial.printf("turn %+d deg (HOLD / AUTO)\n", deg);
  } else if (!strcmp(cmd, "STICKS")) {
    source = SRC_STICKS;
    Serial.println("pilot control");
  } else if (!strcmp(cmd, "SET") && argc == 3) {
    for (char* c = argv[1]; *c; ++c) *c = tolower(*c);
    sendParam(argv[1], atof(argv[2]));
    Serial.printf("sent %s = %s\n", argv[1], argv[2]);
  } else if (!strcmp(cmd, "SAVE")) {
    sendCommand(proto::CMD_SAVE, 0);
    Serial.println("save sent (butterfly must be disarmed)");
  } else if (!strcmp(cmd, "CALIB")) {
    if (!butterflyDisarmed()) { Serial.println("refused: butterfly armed"); return; }
    sendCommand(proto::CMD_CALIB_GYRO, 0);
    Serial.println("gyro calibration sent (keep butterfly still)");
  } else if (!strcmp(cmd, "TEL") && argc == 2) {
    for (char* c = argv[1]; *c; ++c) *c = toupper(*c);
    telemetryPrint = !strcmp(argv[1], "ON");
    if (telemetryPrint)
      Serial.println("# TEL,ms,roll,pitch,yaw,ur,up,uy,vbat,mode,state,flags,flap_hz,link_pps,alt,vz");
  } else if (!strcmp(cmd, "STATUS")) {
    printStatus();
  } else {
    Serial.println("? ARM DISARM MODE TAKEOFF LAND UP DOWN THR LEFT RIGHT TURN STICKS SET SAVE CALIB TEL STATUS");
  }
}

static void pollStream(Stream& s, char* buf, size_t& len, size_t cap) {
  while (s.available()) {
    const char c = (char)s.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[len] = '\0'; handleLine(buf); len = 0; }   // replies go to USB
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
  pinMode(PIN_LED, OUTPUT);
  delay(300);
  oneShotSeq = (uint8_t)random(256);   // avoid seq collisions after a ground-station reboot

#if USE_XBOX
  xbox.begin();
  Serial.println("Xbox: hold the pairing button on top of the controller until the logo flashes fast");
#else
  pinMode(PIN_ARM, INPUT_PULLUP);
  pinMode(PIN_MODE, INPUT_PULLUP);
  analogReadResolution(12);
  if (HAS_STICKS) {   // sticks must be centred at power-up
    centerRoll = averageRead(PIN_ROLL);
    centerPitch = averageRead(PIN_PITCH);
    centerYaw = averageRead(PIN_YAW);
  }
  lastModeSwitch = digitalRead(PIN_MODE) == LOW;
#endif
  Serial.println(radioBegin() ? "ground station ready (ESP-NOW)" : "ESP-NOW init FAILED");
  Serial.println("type STATUS");
}

void loop() {
  static uint32_t lastSend = 0, lastTelPrint = 0;
  static char usbBuf[96], voiceBuf[96];
  static size_t usbLen = 0, voiceLen = 0;

#if USE_XBOX
  xbox.onLoop();
#endif
  pollStream(Serial, usbBuf, usbLen, sizeof(usbBuf));
  pollStream(Serial1, voiceBuf, voiceLen, sizeof(voiceBuf));

  const uint32_t now = millis();
  if (now - lastSend >= 20) {   // 50 Hz
    const float dt = (now - lastSend) / 1000.0f;
    lastSend = now;

    Pilot p;
    readPilot(p);
    if (p.valid) handleButtons(p);

    if (source == SRC_TEXT && p.valid &&
        (fabsf(p.roll) > OVERRIDE || fabsf(p.pitch) > OVERRIDE || fabsf(p.yaw) > OVERRIDE ||
         fabsf(p.climb - handover.climb) > OVERRIDE || fabsf(p.thr - handover.thr) > 0.1f)) {
      source = SRC_STICKS;
      Serial.println("manual override: pilot control");
    }

    float thr, roll = p.roll, pitch = p.pitch, yaw = p.yaw;
    if (source == SRC_TEXT) {
      roll = pitch = yaw = 0;
      const float step = THR_SLEW * dt;
      vThr += constrain(vThrTarget - vThr, -step, step);
      if (now > textClimbUntil) textClimb = 0;
      thr = mode == proto::MODE_AUTO ? 0.5f + 0.5f * textClimb : vThr;
    } else {
      thr = mode == proto::MODE_AUTO ? 0.5f + 0.5f * p.climb : p.thr;
    }

    // No pilot device (e.g. Xbox out of range) and no text control: stop sending,
    // so the butterfly enters failsafe (stabilised glide) instead of flying on stale input.
    const bool haveControl = p.valid || source == SRC_TEXT;
    if (haveControl) {
      proto::ControlPacket c = {};
      c.thr = (int16_t)lroundf(thr * 1000);
      c.roll = (int16_t)lroundf(roll * 500);
      c.pitch = (int16_t)lroundf(pitch * 500);
      c.yaw = (int16_t)lroundf(yaw * 500);
      c.mode = mode;
      c.armed = armed ? 1 : 0;
      proto::seal(c, proto::PKT_CONTROL, NET_ID, ctrlSeq++);
      esp_now_send(kBroadcast, reinterpret_cast<const uint8_t*>(&c), sizeof(c));
    }
  }

  if (telemetryPrint && telFresh && now - lastTelPrint >= 50) {
    telFresh = false;
    lastTelPrint = now;
    proto::TelemetryPacket t;
    portENTER_CRITICAL(&telMux);
    t = lastTel;
    portEXIT_CRITICAL(&telMux);
    Serial.printf("TEL,%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%u,%u,%.1f,%u,%.2f,%.2f\n", (unsigned long)t.ms,
                  t.roll_cd / 100.0f, t.pitch_cd / 100.0f, t.yaw_cd / 100.0f, t.ur_cd / 100.0f,
                  t.up_cd / 100.0f, t.uy_cd / 100.0f, t.vbat_mv / 1000.0f, t.mode, t.state, t.flags,
                  t.flap_dhz / 10.0f, t.link_pps, t.alt_cm / 100.0f, t.vz_cms / 100.0f);
  }

  // LED: on while telemetry is arriving, blinking when the butterfly is silent
  const bool linked = lastTelMs != 0 && now - lastTelMs < 500;
  digitalWrite(PIN_LED, (linked || (now / 250) % 2) ? LOW : HIGH);
  delay(1);
}

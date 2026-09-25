// Butterfly ground station: GameSir G7 Pro gamepad (Bluetooth) -> ESP-NOW -> butterfly
//
// Board: DFRobot FireBeetle 2 ESP32-E (an ORIGINAL ESP32: Classic Bluetooth + BLE).
// Arduino board package "esp32_bluepad32" (Bluepad32 gamepad host), see firmware/README.md.
// G7 Pro: mode switch on the back to Bluetooth, press the Xbox button, hold the pairing button.
//
// Button map (G7 Pro, Xbox layout):
//   Left stick  up/down    AUTO: climb / descend (release = hold altitude)
//   Left stick  left/right yaw (turn on the spot / change heading)
//   Right stick up/down    push = nose down = faster, pull = nose up = slower
//   Right stick left/right bank left / right
//   RT                     throttle in MANUAL / STAB / HOLD
//   A (hold 1 s) arm       B disarm
//   D-pad  up AUTO | right HOLD | down STAB | left MANUAL
//   Y  U-turn 180 deg      LB / RB  turn 45 deg left / right (HOLD, AUTO)
//   X  return to home (GPS); any stick or D-pad takes back control
//   View  gyro calibration (disarmed)
//
// Text commands (voice / AI / PC) on USB Serial and Serial1 RX (voice pin), one per line:
//   ARM | DISARM | MODE MANUAL|STAB|HOLD|AUTO|RTH | RTH | TAKEOFF | LAND | UP | DOWN | THR <0..1>
//   LEFT [deg] | RIGHT [deg] | TURN <deg> | STICKS | SET <param> <value> | SAVE | CALIB
//   TEL ON|OFF | STATUS | PAIR (forget paired gamepads and accept a new one)
// Moving a stick takes control back from text commands immediately (human override).
//
// Phone dashboard: join Wi-Fi "Butterfly-GS" (password butterfly123), open http://192.168.4.1
// for live GPS position, track, telemetry and the optional camera stream.
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "protocol.h"

// ---------------- configuration ----------------
constexpr uint8_t NET_ID = 1;             // must match butterfly param net_id
constexpr uint32_t VOICE_BAUD = 115200;
constexpr int PIN_VOICE_RX = 16;          // GPIO16 Serial1 RX: optional voice module TX
constexpr int PIN_LED = 2;                // on-board LED of the FireBeetle 2 ESP32-E (active high)

constexpr float TAKEOFF_THR = 0.75f;      // text TAKEOFF throttle in non-AUTO modes
constexpr float THR_SLEW = 0.4f;          // text-command throttle ramp (per second)
constexpr float OVERRIDE = 0.25f;         // stick deflection that cancels text control

#ifndef ENABLE_WEB
#define ENABLE_WEB 1                      // phone dashboard over the ground station's Wi-Fi AP
#endif
constexpr const char* AP_SSID = "Butterfly-GS";
constexpr const char* AP_PASS = "butterfly123";                    // at least 8 characters
constexpr const char* CAMERA_STREAM = "http://192.168.4.50:81/stream";  // camera_node address
#if ENABLE_WEB
#include <WebServer.h>
#include "web_ui.h"
static WebServer web(80);
#endif

#include <Bluepad32.h>
static ControllerPtr pad = nullptr;       // the first gamepad that connects is used
constexpr float DEADBAND = 0.08f;         // stick dead zone
constexpr uint32_t ARM_HOLD_MS = 1000;    // hold A this long to arm

// ---------------- state ----------------
static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t oneShotSeq = 0, ctrlSeq = 0;

// Normalised pilot input from the gamepad.
struct Pilot {
  bool valid = false;               // device connected and delivering data
  float thr = 0;                    // 0..1 (MANUAL / STAB / HOLD)
  float climb = 0;                  // -1..1 (AUTO), + = up
  float roll = 0, pitch = 0, yaw = 0;  // -1..1, pitch + = nose up
};

// Gamepad buttons in Xbox layout (defined up here: the .ino preprocessor puts function
// prototypes before the first function, so types used in signatures must come first).
struct PadButtons {
  bool a, b, x, y, lb, rb, up, right, down, left, view;
};

enum Source { SRC_STICKS, SRC_TEXT };
static Source source = SRC_STICKS;
static uint8_t mode = proto::MODE_MANUAL;
static bool armed = false;               // A (hold) arms, B disarms
static float vThr = 0, vThrTarget = 0, textClimb = 0;
static uint32_t textClimbUntil = 0;
static Pilot handover;                   // stick positions when text control started
static bool telemetryPrint = false;

static portMUX_TYPE telMux = portMUX_INITIALIZER_UNLOCKED;
static proto::TelemetryPacket lastTel;
static uint32_t lastTelMs = 0;
static volatile bool telFresh = false;

// ---------------- radio ----------------
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
#else   // Arduino-ESP32 2.x (the Bluepad32 board package)
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
#endif
  proto::TelemetryPacket t;
  if (!proto::open(data, len, proto::PKT_TELEMETRY, NET_ID, t)) return;
  portENTER_CRITICAL(&telMux);
  lastTel = t;
  lastTelMs = millis();
  portEXIT_CRITICAL(&telMux);
  telFresh = true;
}

static bool radioBegin() {
#if ENABLE_WEB
  // The access point sits on the ESP-NOW channel, so both share the one radio.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS, proto::WIFI_CHANNEL);
#else
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(proto::WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
#endif
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
    case proto::MODE_RTH: return "RTH";
    default: return "?";
  }
}

static float deadband(float v) {
  if (fabsf(v) < DEADBAND) return 0;
  return constrain((v - (v > 0 ? DEADBAND : -DEADBAND)) / (1.0f - DEADBAND), -1.0f, 1.0f);
}

// ---------------- input: gamepad ----------------
static void onPadConnected(ControllerPtr c) {
  if (pad == nullptr) {
    pad = c;
    Serial.printf("gamepad connected: %s (battery %d%%)\n", c->getModelName().c_str(), c->battery() * 100 / 255);
  } else {
    c->disconnect();      // one pilot at a time
  }
}

static void onPadDisconnected(ControllerPtr c) {
  if (pad == c) {
    pad = nullptr;
    Serial.println("gamepad disconnected");
  }
}

static bool padConnected() { return pad != nullptr && pad->isConnected() && pad->isGamepad(); }

static void rumble(uint16_t ms) {
  if (padConnected()) pad->playDualRumble(0, ms, 0x60, 0x60);
}

static void readPilot(Pilot& p) {
  p = Pilot();
  if (!padConnected()) return;
  auto axis = [](int32_t v) { return deadband(constrain(v / 512.0f, -1.0f, 1.0f)); };  // -511 .. 512
  p.valid = true;
  p.yaw = axis(pad->axisX());
  p.climb = -axis(pad->axisY());          // stick up is negative
  p.roll = axis(pad->axisRX());
  p.pitch = axis(pad->axisRY());          // pull back (+) = nose up
  p.thr = constrain(pad->throttle() / 1023.0f, 0.0f, 1.0f);   // RT: 0 .. 1023
  if (p.thr < 0.02f) p.thr = 0;
}

static PadButtons readButtons() {
  const uint8_t d = pad->dpad();
  return {pad->a(), pad->b(), pad->x(), pad->y(), pad->l1(), pad->r1(), (d & DPAD_UP) != 0,
          (d & DPAD_RIGHT) != 0, (d & DPAD_DOWN) != 0, (d & DPAD_LEFT) != 0, pad->miscSelect()};
}


static void handleButtons(const Pilot& p) {
  static bool prev[11] = {false};
  static uint32_t aSince = 0;
  const PadButtons b = readButtons();
  const bool now[11] = {b.a, b.b, b.y, b.lb, b.rb, b.up, b.right, b.down, b.left, b.view, b.x};
  auto pressed = [&](int i) { return now[i] && !prev[i]; };

  // A held for 1 s arms, only with the throttle channel in its safe position.
  static bool armHandled = false;
  if (now[0]) {
    if (aSince == 0) { aSince = millis(); armHandled = false; }
    if (!armed && !armHandled && millis() - aSince >= ARM_HOLD_MS) {
      armHandled = true;
      const bool centred = mode == proto::MODE_AUTO || mode == proto::MODE_RTH;
      const bool thrSafe = centred ? p.climb == 0 : p.thr == 0;
      if (thrSafe) { armed = true; rumble(250); }
      Serial.println(thrSafe ? "ARMED (A)"
                     : centred ? "arm refused: centre the left stick" : "arm refused: release RT");
    }
  } else {
    aSince = 0;
  }
  if (pressed(1) && armed) { armed = false; rumble(120); Serial.println("DISARMED (B)"); }
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
  if (pressed(10)) { mode = proto::MODE_RTH; source = SRC_STICKS; rumble(400); Serial.println("RETURN TO HOME (X)"); }
  // Any real stick input during RTH hands control back in AUTO.
  if (mode == proto::MODE_RTH && (fabsf(p.roll) > OVERRIDE || fabsf(p.pitch) > OVERRIDE ||
                                  fabsf(p.yaw) > OVERRIDE || fabsf(p.climb) > OVERRIDE)) {
    mode = proto::MODE_AUTO;
    Serial.println("RTH cancelled by stick: AUTO");
  }
  memcpy(prev, now, sizeof(prev));
}


// ---------------- phone dashboard ----------------
static double homeLat = 0, homeLon = 0;
static bool homeSet = false;

// Home = position when the butterfly arms (or the first fix if it never armed yet).
static void updateHome() {
  static uint8_t prevState = proto::ST_DISARMED;
  proto::TelemetryPacket t;
  uint32_t age;
  portENTER_CRITICAL(&telMux);
  t = lastTel;
  age = millis() - lastTelMs;
  portEXIT_CRITICAL(&telMux);
  if (lastTelMs == 0 || age > 1000) return;
  const bool fix = t.flags & proto::FLAG_GPS_FIX;
  const bool armEdge = t.state == proto::ST_ARMED && prevState == proto::ST_DISARMED;
  prevState = t.state;
  if (fix && (!homeSet || armEdge)) {
    homeLat = t.lat_e7 / 1e7;
    homeLon = t.lon_e7 / 1e7;
    homeSet = true;
  }
}

#if ENABLE_WEB
static void handleApi() {
  proto::TelemetryPacket t;
  uint32_t age;
  portENTER_CRITICAL(&telMux);
  t = lastTel;
  age = millis() - lastTelMs;
  portEXIT_CRITICAL(&telMux);
  const bool ok = lastTelMs != 0 && age < 1000;
  const bool fix = ok && (t.flags & proto::FLAG_GPS_FIX);
  const bool padOk = padConnected();
  char home[64] = "null";
  if (homeSet) snprintf(home, sizeof(home), "[%.7f,%.7f]", homeLat, homeLon);
  char buf[640];
  snprintf(buf, sizeof(buf),
           "{\"ok\":%s,\"age\":%lu,\"state\":%u,\"mode\":%u,\"flags\":%u,\"vbat\":%.2f,\"alt\":%.2f,"
           "\"vz\":%.2f,\"roll\":%.1f,\"pitch\":%.1f,\"yaw\":%.1f,\"flap\":%.1f,\"link\":%u,"
           "\"fix\":%s,\"lat\":%.7f,\"lon\":%.7f,\"galt\":%.1f,\"spd\":%.2f,\"crs\":%.1f,\"sats\":%u,"
           "\"hdop\":%.1f,\"home\":%s,\"pad\":%s,\"armed\":%s,\"cam\":\"%s\"}",
           ok ? "true" : "false", (unsigned long)age, t.state, t.mode, t.flags, t.vbat_mv / 1000.0f,
           t.alt_cm / 100.0f, t.vz_cms / 100.0f, t.roll_cd / 100.0f, t.pitch_cd / 100.0f, t.yaw_cd / 100.0f,
           t.flap_dhz / 10.0f, t.link_pps, fix ? "true" : "false", t.lat_e7 / 1e7, t.lon_e7 / 1e7,
           t.gps_alt_dm / 10.0f, t.gspeed_cms / 100.0f, t.course_cd / 100.0f, t.sats, t.hdop_d / 10.0f,
           home, padOk ? "true" : "false", armed ? "true" : "false", CAMERA_STREAM);
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", buf);
}

static void handleSetHome() {
  proto::TelemetryPacket t;
  portENTER_CRITICAL(&telMux);
  t = lastTel;
  portEXIT_CRITICAL(&telMux);
  if (t.flags & proto::FLAG_GPS_FIX) {
    homeLat = t.lat_e7 / 1e7;
    homeLon = t.lon_e7 / 1e7;
    homeSet = true;
  }
  char buf[80];
  if (homeSet) snprintf(buf, sizeof(buf), "{\"home\":[%.7f,%.7f]}", homeLat, homeLon);
  else snprintf(buf, sizeof(buf), "{\"home\":null}");
  web.send(200, "application/json", buf);
}

static void webBegin() {
  web.on("/", []() { web.send(200, "text/html; charset=utf-8", WEB_PAGE); });
  web.on("/api", handleApi);
  web.on("/sethome", handleSetHome);
  web.onNotFound([]() { web.send(404, "text/plain", "not found"); });
  web.begin();
  Serial.printf("phone dashboard: join Wi-Fi \"%s\" (password %s), open http://%s\n", AP_SSID, AP_PASS,
                WiFi.softAPIP().toString().c_str());
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
  if (padConnected())
    Serial.printf("gamepad %s (battery %d%%) | ", pad->getModelName().c_str(), pad->battery() * 100 / 255);
  else
    Serial.print("gamepad: none - put it in Bluetooth pairing mode (type PAIR to forget old pads) | ");
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
    if (t.flags & proto::FLAG_GPS_FIX)
      Serial.printf("GPS: %.7f, %.7f | %u sats | %.1f m/s | home %s%s\n", t.lat_e7 / 1e7, t.lon_e7 / 1e7, t.sats,
                    t.gspeed_cms / 100.0f, homeSet ? "set" : "not set",
                    (t.flags & proto::FLAG_RTH) ? " | RETURNING HOME" : "");
    else
      Serial.printf("GPS: no fix (%u sats)\n", t.sats);
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
    armed = true;
    Serial.println("ARMED (text)");
  } else if (!strcmp(cmd, "DISARM")) {
    armed = false;
    Serial.println("DISARMED");
  } else if (!strcmp(cmd, "MODE") && argc == 2) {
    for (char* c = argv[1]; *c; ++c) *c = toupper(*c);
    const char* m = argv[1];
    if (!strcmp(m, "MANUAL")) mode = proto::MODE_MANUAL;
    else if (!strcmp(m, "STAB")) mode = proto::MODE_STABILIZE;
    else if (!strcmp(m, "HOLD")) mode = proto::MODE_HEADING_HOLD;
    else if (!strcmp(m, "AUTO")) mode = proto::MODE_AUTO;
    else if (!strcmp(m, "RTH")) mode = proto::MODE_RTH;
    else { Serial.println("MODE MANUAL|STAB|HOLD|AUTO|RTH"); return; }
    Serial.printf("mode %s\n", m);
  } else if (!strcmp(cmd, "RTH") || !strcmp(cmd, "HOME")) {
    mode = proto::MODE_RTH;
    source = SRC_STICKS;
    Serial.println("RETURN TO HOME (needs GPS fix, home and straight flight for north alignment)");
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
  } else if (!strcmp(cmd, "PAIR")) {
    BP32.forgetBluetoothKeys();
    BP32.enableNewBluetoothConnections(true);
    Serial.println("paired gamepads forgotten; put the gamepad in pairing mode now");
  } else {
    Serial.println("? ARM DISARM MODE RTH TAKEOFF LAND UP DOWN THR LEFT RIGHT TURN STICKS SET SAVE CALIB TEL STATUS");
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

  BP32.setup(&onPadConnected, &onPadDisconnected);
  BP32.enableVirtualDevice(false);
  BP32.enableNewBluetoothConnections(true);
  Serial.println("G7 Pro: mode switch to Bluetooth, press the Xbox button, hold the pairing button");
  Serial.println(radioBegin() ? "ground station ready (ESP-NOW)" : "ESP-NOW init FAILED");
#if ENABLE_WEB
  webBegin();
#endif
  Serial.println("type STATUS");
}

void loop() {
  static uint32_t lastSend = 0, lastTelPrint = 0;
  static char usbBuf[96], voiceBuf[96];
  static size_t usbLen = 0, voiceLen = 0;

  BP32.update();
  pollStream(Serial, usbBuf, usbLen, sizeof(usbBuf));
  pollStream(Serial1, voiceBuf, voiceLen, sizeof(voiceBuf));
  updateHome();
#if ENABLE_WEB
  web.handleClient();
#endif

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
      thr = mode == proto::MODE_AUTO || mode == proto::MODE_RTH ? 0.5f + 0.5f * textClimb : vThr;
    } else {
      thr = mode == proto::MODE_AUTO || mode == proto::MODE_RTH ? 0.5f + 0.5f * p.climb : p.thr;
    }

    // No gamepad (e.g. out of range or switched off) and no text control: stop sending,
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
  digitalWrite(PIN_LED, (linked || (now / 250) % 2) ? HIGH : LOW);
  delay(1);
}

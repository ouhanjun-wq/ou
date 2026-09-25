// 6-DOF robot arm (5 joints + gripper) controlled by a GameSir G7 Pro gamepad.
//
// Board: ESP32 DevKit (ESP32-WROOM-32E, an ORIGINAL ESP32 with Classic Bluetooth + BLE).
// Arduino board package "esp32_bluepad32" (Bluepad32 gamepad host), see firmware/README.md.
// G7 Pro: mode switch on the back to Bluetooth, press the Xbox button, hold the pairing button.
//
// Gamepad (Xbox layout)                JOINT mode              CARTESIAN (XYZ) mode
//   Left stick  left / right           J1 base                 tool left / right
//   Left stick  up / down              J2 shoulder             tool forward / back
//   Right stick up / down              J3 elbow                tool up / down
//   Right stick left / right           J4 wrist pitch          tool pitch
//   D-pad left / right                 J5 wrist roll           J5 wrist roll
//   RT / LT                            close / open the gripper (stops by itself on an object)
//   Menu (≡)  servo power on / park + power off      View (⧉)  switch JOINT / XYZ
//   Y  home pose      B  stop (also resumes after ST_OVERLOAD)     LB / RB  speed - / +
//   A  record waypoint (hold 2 s: clear all)        X  play once (hold 1 s: loop)
//   D-pad down  delete last waypoint                D-pad up (hold 1 s)  save waypoints to flash
//
// Text commands (commands.h, cli.cpp) on USB serial, the voice-module UART and the web page.
#include <Bluepad32.h>
#include <WiFi.h>
#include <Wire.h>

#include "app.h"
#include "commands.h"
#include "config.h"
#include "ina226.h"
#include "storage.h"

#if ENABLE_WEB
#include <WebServer.h>
#include "web_ui.h"
static WebServer web(80);
#endif

Params P;
arm::ArmCore core(P);
arm::Sensors sensors;
PCA9685 pwm;
float rawUs[NJ] = {0};

static INA226 ina;
static ControllerPtr pad = nullptr;
static char lastEvent[96] = "";
static uint8_t beepsLeft = 0;
static uint32_t beepNext = 0;
static bool beepOn = false;

// ---------------- gamepad ----------------
static void onPadConnected(ControllerPtr c) {
  if (pad == nullptr) {
    pad = c;
    Serial.printf("gamepad connected: %s (battery %d%%)\n", c->getModelName().c_str(), c->battery() * 100 / 255);
    c->playDualRumble(0, 200, 0x60, 0x60);
  } else {
    c->disconnect();      // one operator at a time
  }
}

static void onPadDisconnected(ControllerPtr c) {
  if (pad == c) {
    pad = nullptr;
    Serial.println("gamepad disconnected (arm holds its pose)");
  }
}

static bool padConnected() { return pad != nullptr && pad->isConnected() && pad->isGamepad(); }

static arm::PadInput readPad() {
  arm::PadInput in;
  if (!padConnected()) return in;
  auto axis = [](int32_t v) { return constrain(v / 512.0f, -1.0f, 1.0f); };   // -511 .. 512
  const uint8_t d = pad->dpad();
  in.valid = true;
  in.lx = axis(pad->axisX());
  in.ly = -axis(pad->axisY());            // stick up is negative on the pad
  in.rx = axis(pad->axisRX());
  in.ry = -axis(pad->axisRY());
  in.rt = constrain(pad->throttle() / 1023.0f, 0.0f, 1.0f);   // RT 0 .. 1023
  in.lt = constrain(pad->brake() / 1023.0f, 0.0f, 1.0f);      // LT 0 .. 1023
  in.a = pad->a();
  in.b = pad->b();
  in.x = pad->x();
  in.y = pad->y();
  in.lb = pad->l1();
  in.rb = pad->r1();
  in.up = d & DPAD_UP;
  in.down = d & DPAD_DOWN;
  in.left = d & DPAD_LEFT;
  in.right = d & DPAD_RIGHT;
  in.view = pad->miscSelect();
  in.menu = pad->miscStart();
  return in;
}

// ---------------- outputs ----------------
static void applyOutputs() {
  static bool prevPower = false;
  if (core.power) {
    for (int j = 0; j < NJ; ++j) pwm.writeUs(SERVO_CH[j], jointPulse(j));
    if (!prevPower) delay(25);             // one full PWM frame at the right pulse before power arrives
    digitalWrite(PIN_RELAY, HIGH);
  } else {
    digitalWrite(PIN_RELAY, LOW);
    if (prevPower) delay(20);
    pwm.allOff();
    for (int j = 0; j < NJ; ++j) rawUs[j] = 0;
  }
  prevPower = core.power;
}

static void beepService(uint32_t now) {
  if (beepsLeft == 0 && !beepOn) return;
  if ((int32_t)(now - beepNext) < 0) return;
  beepOn = !beepOn;
  digitalWrite(PIN_BUZZER, beepOn ? HIGH : LOW);
  if (beepOn) --beepsLeft;
  beepNext = now + (beepOn ? 80 : 120);
}

// ---------------- text lines (USB, voice UART, web) ----------------
static void handleLine(const char* line, char* reply, size_t n) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%s", line);
  if (arm::textCommand(core, sensors, buf, reply, n)) return;
  snprintf(buf, sizeof(buf), "%s", line);
  if (cliCommand(buf, reply, n)) return;
  snprintf(reply, n, "error: unknown command (HELP)");
}

static void pollStream(Stream& s, char* buf, size_t& len, size_t cap, const char* who) {
  while (s.available()) {
    const char c = (char)s.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[len] = '\0';
      len = 0;
      if (buf[0] == '\0') continue;
      char reply[600];
      handleLine(buf, reply, sizeof(reply));
      if (reply[0]) Serial.printf("%s%s\n", who, reply);
    } else if (len < cap - 1) {
      buf[len++] = c;
    }
  }
}

// ---------------- web page ----------------
#if ENABLE_WEB
static void handleApi() {
  const kin::Pose p = core.pose();
  char buf[640];
  snprintf(buf, sizeof(buf),
           "{\"state\":\"%s\",\"act\":\"%s\",\"mode\":\"%s\",\"speed\":%d,\"q\":[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f],"
           "\"pose\":[%.1f,%.1f,%.1f,%.1f,%.1f],\"sensor\":%s,\"amps\":%.2f,\"volts\":%.2f,\"pad\":%s,\"wp\":%d,"
           "\"event\":\"%s\"}",
           arm::stateName(core.state), arm::activityName(core.activity), core.mode == arm::CART_MODE ? "CART" : "JOINT",
           core.speedLvl, core.q[0], core.q[1], core.q[2], core.q[3], core.q[4], core.q[5], p.x, p.y, p.z, p.pitch,
           p.roll, sensors.ok ? "true" : "false", sensors.amps, sensors.volts, padConnected() ? "true" : "false",
           core.seqLen, lastEvent);
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", buf);
}

static void handleCmd() {
  char reply[600];
  handleLine(web.arg("c").c_str(), reply, sizeof(reply));
  Serial.printf("web> %s\n", reply);
  web.send(200, "text/plain; charset=utf-8", reply);
}

static void webBegin() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  web.on("/", []() { web.send(200, "text/html; charset=utf-8", WEB_PAGE); });
  web.on("/api", handleApi);
  web.on("/cmd", handleCmd);
  web.onNotFound([]() { web.send(404, "text/plain", "not found"); });
  web.begin();
  Serial.printf("phone page: join Wi-Fi \"%s\" (password %s), open http://%s\n", AP_SSID, AP_PASS,
                WiFi.softAPIP().toString().c_str());
}
#endif

// ---------------- setup / loop ----------------
void setup() {
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);            // servo power stays off until Menu / ON
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(115200);
  Serial2.begin(VOICE_BAUD, SERIAL_8N1, PIN_VOICE_RX, PIN_VOICE_TX);
  delay(300);

  paramsDefaults(P);
  Serial.println(loadParams(P) ? "parameters loaded from flash" : "no saved parameters: DEFAULTS (calibrate first!)");
  core.reset();
  core.seqLen = loadWaypoints(core.seq, arm::MAX_WP);
  Serial.printf("%d waypoints loaded\n", core.seqLen);

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Serial.println(pwm.begin(Wire, PCA9685_ADDR, P.pwm_osc_hz, PWM_HZ) ? "PCA9685 ok"
                                                                        : "PCA9685 NOT FOUND at 0x41 (A0 bridged?)");
  Serial.println(ina.begin(Wire, INA226_ADDR) ? "INA226 ok (over-current / E-stop / grip detection on)"
                                              : "INA226 not found: protection by current is OFF");

  BP32.setup(&onPadConnected, &onPadDisconnected);
  BP32.enableVirtualDevice(false);
  BP32.enableNewBluetoothConnections(true);
  Serial.println("G7 Pro: mode switch to Bluetooth, press the Xbox button, hold the pairing button");
#if ENABLE_WEB
  webBegin();
#endif
  Serial.println("ready. Put the arm in the PARK pose, then press Menu (or type ON). HELP lists commands.");
}

void loop() {
  static uint32_t last = micros();
  static char usbBuf[128], voiceBuf[128];
  static size_t usbLen = 0, voiceLen = 0;

  BP32.update();
  pollStream(Serial, usbBuf, usbLen, sizeof(usbBuf), "");
  pollStream(Serial2, voiceBuf, voiceLen, sizeof(voiceBuf), "voice> ");
#if ENABLE_WEB
  web.handleClient();
#endif

  const uint32_t now = micros();
  if (now - last >= LOOP_US) {
    const float dt = fminf((now - last) * 1e-6f, 0.05f);
    last = now;
    sensors.ok = ina.read(P.shunt_ohm, sensors.amps, sensors.volts);
    core.update(readPad(), sensors, dt);
    applyOutputs();
    if (core.event) {
      snprintf(lastEvent, sizeof(lastEvent), "%s", core.event);
      Serial.printf("[arm] %s\n", core.event);
    }
    if (core.rumbleMs && padConnected()) pad->playDualRumble(0, core.rumbleMs, 0x70, 0x70);
    if (core.beeps) { beepsLeft = core.beeps; beepNext = millis(); }
    if (core.saveRequest) {
      core.saveRequest = false;
      Serial.println(saveWaypoints(core.seq, core.seqLen) ? "waypoints saved" : "waypoint save FAILED");
    }
  }
  beepService(millis());
  // LED: steady = gamepad connected, slow blink = waiting for the gamepad, fast blink = fault / e-stop
  const uint32_t ms = millis();
  const bool bad = core.state == arm::ST_ESTOP || core.state == arm::ST_FAULT || core.state == arm::ST_OVERLOAD;
  digitalWrite(PIN_LED, bad ? (ms / 100) % 2 : padConnected() ? HIGH : (ms / 500) % 2);
  delay(1);
}

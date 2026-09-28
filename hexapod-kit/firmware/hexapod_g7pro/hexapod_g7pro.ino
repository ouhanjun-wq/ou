// 18-servo hexapod kit (original ESP32 WROOM board) driven by a GameSir G7 Pro over Bluetooth.
//
// Board package: "esp32_bluepad32" (Bluepad32 for Arduino), board "ESP32 Dev Module".
//   Additional boards manager URL:
//   https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
// Only the original ESP32 works: it has Classic Bluetooth, which the G7 Pro uses in Bluetooth mode.
//
// Boot: servos OFF (limp). Lay the robot on its belly, then press A (or type "stand").
// Gamepad buttons: see teleop.h.  USB serial 115200: type "help".
//
// Flashing this replaces the seller's firmware: the phone app stops working.
#include <Arduino.h>
#include <Bluepad32.h>
#include <stdlib.h>
#include <string.h>

#include "params.h"
#include "servo_out.h"

static gait::Hexapod hex{gait::Config()};
static teleop::Teleop tel{teleop::Limits()};

// ---------------- gamepad (Bluepad32) ----------------
static ControllerPtr pad = nullptr;       // the first gamepad that connects is used

static void onPadConnected(ControllerPtr c) {
  if (pad == nullptr) {
    pad = c;
    Serial.printf("gamepad connected: %s (battery %d%%)\n", c->getModelName().c_str(), c->battery() * 100 / 255);
    c->playDualRumble(0, 200, 0x60, 0x60);
  } else {
    c->disconnect();      // one driver at a time
  }
}

static void onPadDisconnected(ControllerPtr c) {
  if (pad == c) {
    pad = nullptr;
    Serial.println("gamepad disconnected - robot stops and stands still");
  }
}

static bool padConnected() { return pad != nullptr && pad->isConnected() && pad->isGamepad(); }

static void rumble(uint16_t ms) {
  if (padConnected()) pad->playDualRumble(0, ms, 0x60, 0x60);
}

static void readPad(teleop::PadInput& in) {
  in = teleop::PadInput();
  if (!padConnected()) return;
  auto axis = [](int32_t v) { return constrain(v / 512.0f, -1.0f, 1.0f); };   // -511 .. 512
  in.connected = true;
  in.lx = axis(pad->axisX());
  in.ly = -axis(pad->axisY());            // stick up is negative in Bluepad32
  in.rx = axis(pad->axisRX());
  in.ry = -axis(pad->axisRY());
  in.a = pad->a();
  in.b = pad->b();
  in.x = pad->x();
  in.y = pad->y();
  in.lb = pad->l1();
  in.rb = pad->r1();
  const uint8_t d = pad->dpad();
  in.up = d & DPAD_UP;
  in.down = d & DPAD_DOWN;
  in.left = d & DPAD_LEFT;
  in.right = d & DPAD_RIGHT;
}

// ---------------- servos ----------------
static const char* const kJointName[3] = {"coxa", "femur", "tibia"};

static void servoName(int n, char* out, size_t len) {
  snprintf(out, len, "%s %s", gait::legName(n / 3), kJointName[n % 3]);
}

static float limitJoint(int joint, float deg) {
  if (joint == 0) return constrain(deg, -P.coxa_lim, P.coxa_lim);
  if (joint == 1) return constrain(deg, P.femur_min, P.femur_max);
  return constrain(deg, P.tibia_min, P.tibia_max);
}

static void outputJoints() {
  for (int leg = 0; leg < gait::NUM_LEGS; ++leg) {
    const kin::Joints& j = hex.joints(leg);
    const float deg[3] = {j.coxa, j.femur, j.tibia};
    for (int k = 0; k < 3; ++k) servo::writeDeg(leg * 3 + k, limitJoint(k, deg[k]), P.us_per_deg);
  }
}

static void powerOff() {
  tel.setMode(teleop::OFF);
  hex.stopNow();
  servo::allOff();
}

static void powerOnStand() {
  if (tel.mode() == teleop::OFF) hex.jumpTo(tel.sitPose());
  tel.setMode(teleop::STAND);
}

// Wiggle one output (probe) so you can see which servo is plugged in there.
struct Probe {
  bool active = false;
  bool all = false;           // step through every candidate output
  bool pca = false;
  uint8_t a = 0, b = 0;       // GPIO pin, or PCA address + channel
  int index = 0;              // "findall" position
  uint32_t startMs = 0, toggleMs = 0;
  bool high = false;
} probe;

static void probeOutput(int us) {
  if (probe.pca) servo::probePca(probe.a, probe.b, us);
  else servo::probeGpio(probe.a, us);
}

static void probeAnnounce() {
  if (probe.pca) Serial.printf("wiggling PCA9685 0x%02X channel %d ... which servo moves?\n", probe.a, probe.b);
  else Serial.printf("wiggling GPIO %d ... which servo moves?\n", probe.a);
}

// findall: PCA channels when a PCA9685 was found, otherwise the probe-safe GPIO pins.
static bool probeTarget(int index) {
  int i = 0;
  for (uint8_t addr = 0x40; addr <= 0x47; ++addr) {
    if (!servo::pcaFound(addr)) continue;
    for (uint8_t ch = 0; ch < 16; ++ch, ++i)
      if (i == index) { probe.pca = true; probe.a = addr; probe.b = ch; return true; }
  }
  if (i > 0) return false;
  const int n = sizeof(servo::kProbePins) / sizeof(servo::kProbePins[0]);
  if (index >= n) return false;
  probe.pca = false;
  probe.a = servo::kProbePins[index];
  return true;
}

static void probeStart() {
  probe.active = true;
  probe.startMs = probe.toggleMs = millis();
  probe.high = false;
  probeAnnounce();
}

static void probeStop() {
  if (probe.active) probeOutput(0);
  probe.active = false;
}

static void probeUpdate() {
  if (!probe.active) return;
  const uint32_t now = millis();
  if (now - probe.toggleMs >= 400) {
    probe.toggleMs = now;
    probe.high = !probe.high;
    probeOutput(probe.high ? 1750 : 1250);
  }
  if (now - probe.startMs >= 3000) {
    probeOutput(0);
    if (probe.all && probeTarget(++probe.index)) {
      probeStart();
    } else {
      probe.active = false;
      Serial.println(probe.all ? "findall done" : "find done");
    }
  }
}

// ---------------- parameters ----------------
static void applyParams() {
  hex.config() = gaitConfig(P);
  tel.limits() = teleopLimits(P);
  tel.setStep(P.step_h);
  tel.setHeight(P.height);
  tel.setGait(int(P.gait));
  hex.setGait(int(P.gait));
}

// ---------------- status ----------------
static const char* modeName(teleop::Mode m) {
  return m == teleop::OFF ? "OFF (servos limp)" : (m == teleop::SIT ? "SIT" : "STAND");
}

static void printStatus() {
  Serial.printf("mode %s | gait %s | speed %d/3 | height %.0f mm | leg lift %.0f mm\n", modeName(tel.mode()),
                gait::typeName(hex.gaitType()), tel.speedLevel(), tel.heightSetting(), tel.stepSetting());
  Serial.printf("velocity vx %.0f vy %.0f mm/s, turn %.0f deg/s | %s%s%s\n", hex.vx(), hex.vy(), hex.wz(),
                hex.walking() ? "walking" : "standing", tel.estopped() ? " | E-STOP (release sticks)" : "",
                hex.reachable() ? "" : " | WARNING: foot target out of reach");
  if (padConnected())
    Serial.printf("gamepad: %s, battery %d%%\n", pad->getModelName().c_str(), pad->battery() * 100 / 255);
  else
    Serial.println("gamepad: none - switch the G7 Pro to Bluetooth mode and put it in pairing mode");
  Serial.print("PCA9685 found at:");
  bool any = false;
  for (uint8_t a = 0x40; a <= 0x47; ++a)
    if (servo::pcaFound(a)) { Serial.printf(" 0x%02X", a); any = true; }
  Serial.println(any ? "" : " none (direct GPIO board?)");
}

static void printMaps() {
  char name[20];
  for (int n = 0; n < servo::NUM; ++n) {
    const servo::Map& m = servo::T.m[n];
    servoName(n, name, sizeof name);
    Serial.printf("%2d %-12s ", n, name);
    if (m.kind == servo::PCA) Serial.printf("pca 0x%02X ch %-2d  ", m.a, m.b);
    else if (m.kind == servo::GPIO) Serial.printf("gpio %-2d         ", m.a);
    else Serial.print("none            ");
    Serial.printf("dir %+d  trim %+d us  now %d us\n", m.dir, m.trim_us, servo::lastUs(n));
  }
}

static void printHelp() {
  Serial.println(
      "--- robot ---\n"
      "  status                 mode, gait, gamepad, boards found\n"
      "  stand | sit | off      off = all servos limp\n"
      "  walk <vx> <vy> <wz> [s]  e.g. walk 60 0 0 2  (mm/s, mm/s, deg/s, seconds)\n"
      "  gait tripod|ripple|wave\n"
      "  pair                   forget the paired gamepad and accept a new one\n"
      "--- finding the servo plugs (robot OFF) ---\n"
      "  i2c                    list I2C devices (0x40..0x47 = PCA9685 servo chip)\n"
      "  find <gpio>            wiggle one ESP32 pin for 3 s\n"
      "  findpca <addr> <ch>    wiggle one PCA9685 channel, e.g. findpca 0x40 3\n"
      "  findall                wiggle every output in turn (stop = stop)\n"
      "  map <n> gpio <pin> | map <n> pca <addr> <ch> | map <n> none\n"
      "  maps                   show the servo table\n"
      "--- servo setup ---\n"
      "  centerall              1500 us on EVERY output (map not needed) - fit the horns now\n"
      "  center                 every mapped servo to joint angle 0 (uses dir / trim)\n"
      "  servo <n> <us>         raw pulse to servo n (0..17)\n"
      "  joint <n> <deg>        joint angle (uses dir / trim)\n"
      "  dir <n> <1|-1>   trim <n> <us>\n"
      "--- parameters ---\n"
      "  params | get <name> | set <name> <value>\n"
      "  save | load | reset    (save = parameters + servo table to flash)\n"
      "servo n = leg*3 + joint; legs 0 LF 1 LM 2 LR 3 RF 4 RM 5 RR; joints 0 coxa 1 femur 2 tibia");
}

// ---------------- serial command line ----------------
static uint32_t walkUntilMs = 0;
static float walkVx = 0, walkVy = 0, walkWz = 0;

static bool parseServo(const char* s, int& n) {
  if (!s) return false;
  n = atoi(s);
  if (n < 0 || n >= servo::NUM) {
    Serial.println("servo number must be 0..17");
    return false;
  }
  return true;
}

static uint8_t parseByte(const char* s) { return uint8_t(strtol(s, nullptr, 0)); }   // "0x40" or "64"

static void manualMode() {
  if (tel.mode() != teleop::OFF) {
    tel.setMode(teleop::OFF);
    hex.stopNow();
    Serial.println("(robot switched OFF for manual servo control; 'stand' or A to resume)");
  }
}

static bool robotOffOrSay() {
  if (tel.mode() == teleop::OFF) return true;
  Serial.println("type 'off' first (servos must be limp while probing)");
  return false;
}

static void handleLine(char* line) {
  const char* cmd = strtok(line, " \t");
  if (!cmd) return;
  const char* a1 = strtok(nullptr, " \t");
  const char* a2 = strtok(nullptr, " \t");
  const char* a3 = strtok(nullptr, " \t");
  const char* a4 = strtok(nullptr, " \t");
  int n = 0;

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    printHelp();
  } else if (!strcmp(cmd, "status")) {
    printStatus();
  } else if (!strcmp(cmd, "stand")) {
    probeStop();
    powerOnStand();
    Serial.println("standing up");
  } else if (!strcmp(cmd, "sit")) {
    if (tel.mode() == teleop::OFF) powerOnStand();
    tel.setMode(teleop::SIT);
    hex.stopNow();
    Serial.println("sitting down");
  } else if (!strcmp(cmd, "off")) {
    probeStop();
    powerOff();
    Serial.println("servos OFF");
  } else if (!strcmp(cmd, "walk")) {
    if (tel.mode() != teleop::STAND) { Serial.println("type 'stand' first"); return; }
    walkVx = a1 ? atof(a1) : 0;
    walkVy = a2 ? atof(a2) : 0;
    walkWz = a3 ? atof(a3) : 0;
    const float s = constrain(a4 ? atof(a4) : 2.0f, 0.1f, 30.0f);
    walkUntilMs = millis() + uint32_t(s * 1000);
    Serial.printf("walking vx %.0f vy %.0f wz %.0f for %.1f s\n", walkVx, walkVy, walkWz, s);
  } else if (!strcmp(cmd, "gait")) {
    int g = -1;
    for (int t = 0; t < gait::NUM_TYPES; ++t)
      if (a1 && !strcmp(a1, gait::typeName(t))) g = t;
    if (g < 0) { Serial.println("gait tripod|ripple|wave"); return; }
    tel.setGait(g);
    hex.setGait(g);
    Serial.printf("gait %s%s\n", gait::typeName(g), hex.walking() ? " (after the robot stops)" : "");
  } else if (!strcmp(cmd, "pair")) {
    if (pad) pad->disconnect();
    BP32.forgetBluetoothKeys();
    BP32.enableNewBluetoothConnections(true);
    Serial.println("old pairings forgotten - put the G7 Pro in Bluetooth pairing mode now");
  } else if (!strcmp(cmd, "i2c")) {
    const int count = servo::scanI2c([](uint8_t a) {
      Serial.printf("  0x%02X%s\n", a, (a >= 0x40 && a <= 0x47) ? "  PCA9685 servo chip" : (a == 0x70 ? "  (PCA9685 all-call)" : ""));
    });
    Serial.printf("%d device(s) (SDA %d, SCL %d)\n", count, int(P.i2c_sda), int(P.i2c_scl));
  } else if (!strcmp(cmd, "find")) {
    if (!a1 || !robotOffOrSay()) return;
    probeStop();
    probe.all = false;
    probe.pca = false;
    probe.a = uint8_t(atoi(a1));
    probeStart();
  } else if (!strcmp(cmd, "findpca")) {
    if (!a1 || !a2 || !robotOffOrSay()) return;
    probeStop();
    probe.all = false;
    probe.pca = true;
    probe.a = parseByte(a1);
    probe.b = uint8_t(atoi(a2) & 15);
    if (!servo::pcaFound(probe.a)) { Serial.println("no PCA9685 at that address (try 'i2c')"); return; }
    probeStart();
  } else if (!strcmp(cmd, "findall")) {
    if (!robotOffOrSay()) return;
    probeStop();
    probe.all = true;
    probe.index = 0;
    if (probeTarget(0)) probeStart();
  } else if (!strcmp(cmd, "stop")) {
    probeStop();
    walkUntilMs = 0;
    Serial.println("stopped");
  } else if (!strcmp(cmd, "map")) {
    if (!parseServo(a1, n) || !a2) return;
    servo::off(n);
    servo::Map& m = servo::T.m[n];
    if (!strcmp(a2, "gpio") && a3) { m.kind = servo::GPIO; m.a = uint8_t(atoi(a3)); m.b = 0; }
    else if (!strcmp(a2, "pca") && a3 && a4) { m.kind = servo::PCA; m.a = parseByte(a3); m.b = uint8_t(atoi(a4) & 15); }
    else if (!strcmp(a2, "none")) { m.kind = servo::NONE; }
    else { Serial.println("map <n> gpio <pin> | map <n> pca <addr> <ch> | map <n> none"); return; }
    printMaps();
  } else if (!strcmp(cmd, "maps")) {
    printMaps();
  } else if (!strcmp(cmd, "centerall")) {
    manualMode();
    probeStop();
    int count = 0;
    for (int i = 0; probeTarget(i); ++i, ++count) {
      if (probe.pca) servo::probePca(probe.a, probe.b, servo::US_MID);
      else servo::probeGpio(probe.a, servo::US_MID);
    }
    Serial.printf("1500 us on %d outputs (%s) - 'off' when done\n", count, probe.pca ? "PCA9685 channels" : "GPIO pins");
  } else if (!strcmp(cmd, "center")) {
    manualMode();
    for (int i = 0; i < servo::NUM; ++i) servo::writeDeg(i, 0, P.us_per_deg);
    Serial.println("all servos at centre (joint angle 0)");
  } else if (!strcmp(cmd, "servo")) {
    if (!parseServo(a1, n) || !a2) return;
    manualMode();
    servo::writeUs(n, atoi(a2));
  } else if (!strcmp(cmd, "joint")) {
    if (!parseServo(a1, n) || !a2) return;
    manualMode();
    servo::writeDeg(n, atof(a2), P.us_per_deg);
    Serial.printf("servo %d -> %d us\n", n, servo::lastUs(n));
  } else if (!strcmp(cmd, "dir")) {
    if (!parseServo(a1, n) || !a2) return;
    servo::T.m[n].dir = atoi(a2) < 0 ? -1 : 1;
    printMaps();
  } else if (!strcmp(cmd, "trim")) {
    if (!parseServo(a1, n) || !a2) return;
    servo::T.m[n].trim_us = int16_t(constrain(atoi(a2), -300, 300));
    if (servo::lastUs(n) > 0 && tel.mode() == teleop::OFF) servo::writeDeg(n, 0, P.us_per_deg);
    Serial.printf("servo %d trim %+d us\n", n, servo::T.m[n].trim_us);
  } else if (!strcmp(cmd, "params")) {
    size_t count = 0;
    const ParamInfo* t = paramTable(count);
    for (size_t i = 0; i < count; ++i) {
      float v = 0;
      paramGet(t[i].name, v);
      Serial.printf("  %-12s %8.2f   [%g .. %g]\n", t[i].name, v, t[i].lo, t[i].hi);
    }
  } else if (!strcmp(cmd, "get")) {
    float v = 0;
    if (a1 && paramGet(a1, v)) Serial.printf("%s = %.2f\n", a1, v);
    else Serial.println("unknown parameter (see 'params')");
  } else if (!strcmp(cmd, "set")) {
    float v = 0;
    if (!a1 || !a2 || !paramSet(a1, atof(a2))) { Serial.println("set <name> <value> (see 'params')"); return; }
    paramGet(a1, v);
    applyParams();
    Serial.printf("%s = %.2f (type 'save' to keep it)\n", a1, v);
  } else if (!strcmp(cmd, "save")) {
    const bool ok = paramsSave() && servo::save();
    Serial.println(ok ? "saved" : "save FAILED");
  } else if (!strcmp(cmd, "load")) {
    paramsLoad();
    servo::load();
    applyParams();
    Serial.println("loaded");
  } else if (!strcmp(cmd, "reset")) {
    paramsDefaults(P);
    if (servo::pcaFound(0x40) || servo::pcaFound(0x41)) servo::defaultPca(servo::T);
    else servo::defaultGpio(servo::T);
    applyParams();
    Serial.println("defaults restored (not saved yet)");
  } else {
    Serial.printf("unknown command '%s' - type help\n", cmd);
  }
}

static void pollSerial() {
  static char buf[96];
  static size_t len = 0;
  while (Serial.available()) {
    const char c = char(Serial.read());
    if (c == '\r' || c == '\n') {
      if (len == 0) continue;
      buf[len] = 0;
      len = 0;
      handleLine(buf);
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

// ---------------- events -> serial + rumble ----------------
static void reportEvents(unsigned ev) {
  if (ev & teleop::EV_POWER_ON) { Serial.println("servos ON"); probeStop(); }
  if (ev & teleop::EV_MODE) { Serial.printf("mode %s\n", modeName(tel.mode())); rumble(120); }
  if (ev & teleop::EV_ESTOP) { Serial.println("E-STOP: release both sticks to drive again"); rumble(400); }
  if (ev & teleop::EV_GAIT) {
    Serial.printf("gait -> %s\n", gait::typeName(tel.pendingGait()));
    rumble(uint16_t(80 * (tel.pendingGait() + 1)));
  }
  if (ev & teleop::EV_SPEED) { Serial.printf("speed %d/3\n", tel.speedLevel()); rumble(60); }
  if (ev & teleop::EV_HEIGHT) Serial.printf("height %.0f mm\n", tel.heightSetting());
  if (ev & teleop::EV_STEP) Serial.printf("leg lift %.0f mm\n", tel.stepSetting());
}

// ---------------- main ----------------
void setup() {
  Serial.begin(115200);
  delay(300);
  const bool saved = paramsLoad();
  servo::begin(int(P.i2c_sda), int(P.i2c_scl));     // servos stay off
  hex = gait::Hexapod(gaitConfig(P));
  tel = teleop::Teleop(teleopLimits(P));
  applyParams();
  gait::Pose sit = tel.sitPose();
  hex.jumpTo(sit);

  BP32.setup(&onPadConnected, &onPadDisconnected);
  BP32.enableVirtualDevice(false);
  BP32.enableNewBluetoothConnections(true);

  Serial.println("\n=== Hexapod + G7 Pro ===");
  Serial.println(saved ? "parameters loaded from flash" : "no saved parameters - using defaults");
  printStatus();
  Serial.println("Servos are OFF. Lay the robot on its belly, then press A (or type 'stand'). 'help' lists commands.");
}

void loop() {
  BP32.update();
  pollSerial();
  probeUpdate();

  static uint32_t last = micros();
  const uint32_t now = micros();
  if (now - last < 20000) {        // 50 Hz control loop
    delay(1);
    return;
  }
  const float dt = constrain((now - last) * 1e-6f, 0.001f, 0.05f);
  last = now;

  teleop::PadInput in;
  readPad(in);
  const unsigned ev = tel.update(dt, in, hex);
  if (walkUntilMs) {                // serial "walk" overrides the (idle) sticks for a while
    if (int32_t(millis() - walkUntilMs) < 0 && tel.mode() == teleop::STAND && !tel.estopped())
      hex.setVelocity(walkVx, walkVy, walkWz);
    else
      walkUntilMs = 0;
  }
  hex.update(dt);
  if (tel.mode() != teleop::OFF) outputJoints();
  if (ev) reportEvents(ev);

  static uint32_t warnMs = 0;
  if (tel.mode() != teleop::OFF && !hex.reachable() && millis() - warnMs > 2000) {
    warnMs = millis();
    Serial.println("warning: a foot target is out of reach - check coxa/femur/tibia lengths ('params')");
  }
}

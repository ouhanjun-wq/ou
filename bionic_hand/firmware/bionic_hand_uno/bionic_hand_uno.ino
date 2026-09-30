// Glove-controlled 6-servo bionic hand on one Arduino Uno R3 (wired).
//
// Board: Arduino Uno R3 + Sensor Shield V5.0 (no soldering). Libraries: Servo, EEPROM (built in).
//   Glove pots (WH148 B10K) -> A0..A5 through an 8-core cable (network cable + RJ45 terminal boards):
//     A0 index  A1 middle  A2 ring  A3 pinky  A4 thumb flex  A5 thumb rotation
//   Servos (MG90S) -> D3 D5 D6 D9 D10 D11 in the same order, powered from the shield's
//     screw terminal (6 V buck), SEL jumper REMOVED so servo current never goes through the Uno.
//   Optional button (learning-kit push button module) D2 -> GND: short press = MIRROR <-> DEMO,
//     hold 2 s = guided glove calibration (open, half, fist: one short press each).
//
// The analog pins use INPUT_PULLUP: with the glove cable unplugged every channel reads ~1023,
// the hand holds its pose for 0.5 s and then slowly opens.
//
// Serial 115200: HELP lists the commands (calibration, servo limits, gestures, modes).
#include <EEPROM.h>
#include <Servo.h>

#include "hand_core.h"

static const uint8_t POT_PIN[hc::N] = {A0, A1, A2, A3, A4, A5};
static const uint8_t SERVO_PIN[hc::N] = {3, 5, 6, 9, 10, 11};
static const uint8_t BUTTON_PIN = 2;
static const uint8_t LED_PIN = 13;
static const uint16_t TICK_MS = 20;
static const char* const CH_NAME[hc::N] = {"index", "middle", "ring", "pinky", "thumb", "t_rot"};

Servo servo[hc::N];
hc::Controller ctrl;
int16_t raw[hc::N];
bool stream = false;

// ---------------- helpers ----------------
static void readPots(int16_t out[hc::N], uint8_t samples) {
  for (uint8_t i = 0; i < hc::N; ++i) {
    uint16_t sum = 0;
    for (uint8_t k = 0; k < samples; ++k) sum += analogRead(POT_PIN[i]);
    out[i] = (int16_t)(sum / samples);
  }
}

static void saveSettings() {
  hc::seal(ctrl.set);
  EEPROM.put(0, ctrl.set);
  Serial.println(F("saved"));
}

static void printRow(uint8_t i) {
  Serial.print(i);
  Serial.print(' ');
  Serial.print(CH_NAME[i]);
  Serial.print(F("\traw "));
  Serial.print(raw[i]);
  Serial.print(F("\tn "));
  Serial.print(ctrl.n[i]);
  Serial.print(F("\tus "));
  Serial.print(ctrl.us[i]);
  Serial.print(F("\tcal "));
  Serial.print(ctrl.set.cal[i].open);
  Serial.print('/');
  Serial.print(ctrl.set.cal[i].half);
  Serial.print('/');
  Serial.print(ctrl.set.cal[i].closed);
  Serial.print(hc::calValid(ctrl.set.cal[i]) ? F(" ok") : F(" BAD"));
  Serial.print(F("\tlim "));
  Serial.print(ctrl.set.lim[i].us_open);
  Serial.print('/');
  Serial.println(ctrl.set.lim[i].us_closed);
}

static void show() {
  static const char* const MODE_NAME[] = {"MIRROR", "GESTURE", "DEMO", "MANUAL"};
  Serial.print(F("mode "));
  Serial.print(MODE_NAME[ctrl.mode]);
  if (ctrl.mode == hc::GESTURE || ctrl.mode == hc::DEMO) {
    Serial.print(' ');
    Serial.print(hc::GESTURES[ctrl.gesture].name);
  }
  Serial.print(F("  glove "));
  Serial.print(ctrl.unplugged ? F("UNPLUGGED") : F("ok"));
  Serial.print(F("  slew "));
  Serial.println(ctrl.set.slew_us);
  for (uint8_t i = 0; i < hc::N; ++i) printRow(i);
}

// Capture one calibration point for all channels: 0 = open, 1 = half, 2 = closed.
static void capture(uint8_t stage) {
  int16_t v[hc::N];
  readPots(v, 32);
  for (uint8_t i = 0; i < hc::N; ++i) {
    hc::Cal& c = ctrl.set.cal[i];
    (stage == 0 ? c.open : stage == 1 ? c.half : c.closed) = v[i];
  }
  Serial.print(F("captured "));
  Serial.println(stage == 0 ? F("OPEN") : stage == 1 ? F("HALF") : F("CLOSED"));
}

static bool calReport() {
  bool ok = true;
  for (uint8_t i = 0; i < hc::N; ++i)
    if (!hc::calValid(ctrl.set.cal[i])) {
      ok = false;
      Serial.print(F("channel "));
      Serial.print(i);
      Serial.println(F(" not monotonic or range < 60 counts: check that pot / linkage"));
    }
  return ok;
}

static void help() {
  Serial.println(F("SHOW | STREAM | CAL OPEN|HALF|CLOSED | LIM ch us_open us_closed | SERVO ch us"));
  Serial.println(F("G name | MODE MIRROR|DEMO | SLEW us | SAVE | DEFAULTS"));
  Serial.print(F("gestures:"));
  for (uint8_t i = 0; i < hc::N_GESTURES; ++i) {
    Serial.print(' ');
    Serial.print(hc::GESTURES[i].name);
  }
  Serial.println();
}

static bool eq(const char* a, const char* b) {   // case-insensitive
  while (*a && *b && (*a | 0x20) == (*b | 0x20)) ++a, ++b;
  return *a == *b;
}

static bool chArg(const char* s, uint8_t& ch) {
  if (!s) return false;
  const int v = atoi(s);
  if (v < 0 || v >= hc::N) return false;
  ch = (uint8_t)v;
  return true;
}

static void command(char* line) {
  char* cmd = strtok(line, " \t");
  if (!cmd) return;
  char* a = strtok(nullptr, " \t");
  char* b = strtok(nullptr, " \t");
  char* c = strtok(nullptr, " \t");
  uint8_t ch;
  if (eq(cmd, "help")) {
    help();
  } else if (eq(cmd, "show")) {
    show();
  } else if (eq(cmd, "stream")) {
    stream = !stream;
  } else if (eq(cmd, "cal") && a) {
    if (eq(a, "open")) capture(0);
    else if (eq(a, "half")) capture(1);
    else if (eq(a, "closed")) capture(2);
    else Serial.println(F("CAL OPEN | CAL HALF | CAL CLOSED"));
    if (eq(a, "closed") && calReport()) Serial.println(F("calibration OK: SAVE to keep it"));
  } else if (eq(cmd, "lim") && chArg(a, ch) && b && c) {
    ctrl.set.lim[ch].us_open = hc::clampv<int16_t>((int16_t)atoi(b), hc::US_MIN, hc::US_MAX);
    ctrl.set.lim[ch].us_closed = hc::clampv<int16_t>((int16_t)atoi(c), hc::US_MIN, hc::US_MAX);
    printRow(ch);
  } else if (eq(cmd, "servo") && chArg(a, ch) && b) {
    if (ctrl.mode != hc::MANUAL) ctrl.setMode(hc::MANUAL);
    ctrl.manual_us[ch] = hc::clampv<int16_t>((int16_t)atoi(b), hc::US_MIN, hc::US_MAX);
  } else if (eq(cmd, "g") && a) {
    const int8_t g = hc::findGesture(a);
    if (g < 0) {
      help();
    } else {
      ctrl.setMode(hc::GESTURE);
      ctrl.gesture = g;
    }
  } else if (eq(cmd, "mode") && a) {
    if (eq(a, "mirror")) ctrl.setMode(hc::MIRROR);
    else if (eq(a, "demo")) ctrl.setMode(hc::DEMO);
  } else if (eq(cmd, "slew") && a) {
    ctrl.set.slew_us = hc::clampv<int16_t>((int16_t)atoi(a), 1, 500);
  } else if (eq(cmd, "save")) {
    saveSettings();
  } else if (eq(cmd, "defaults")) {
    ctrl.set = hc::defaults();
    Serial.println(F("defaults loaded (not saved)"));
  } else {
    Serial.println(F("? (HELP)"));
  }
}

static void pollSerial() {
  static char buf[48];
  static uint8_t len = 0;
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      buf[len] = '\0';
      if (len) command(buf);
      len = 0;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = ch;
    }
  }
}

// ---------------- button: short = MIRROR <-> DEMO, hold 2 s = guided calibration ----------------
static uint8_t calStep = 0;   // 0 = off, 1..3 = waiting for open / half / closed
static void pollButton(uint32_t now) {
  static bool down = false, longDone = false;
  static uint32_t t0 = 0;
  const bool pressed = digitalRead(BUTTON_PIN) == LOW;
  if (pressed && !down) {
    down = true;
    longDone = false;
    t0 = now;
  } else if (pressed && down && !longDone && now - t0 >= 2000) {
    longDone = true;
    calStep = 1;
    Serial.println(F("CALIBRATION: open your hand flat, press"));
  } else if (!pressed && down) {
    down = false;
    if (longDone || now - t0 < 30) return;
    if (calStep) {
      capture(calStep - 1);
      if (calStep == 1) Serial.println(F("half-close every finger (thumb half across), press"));
      if (calStep == 2) Serial.println(F("make a fist, thumb across the fingers, press"));
      if (calStep == 3 && calReport()) saveSettings();
      calStep = calStep == 3 ? 0 : calStep + 1;
    } else {
      ctrl.setMode(ctrl.mode == hc::MIRROR ? hc::DEMO : hc::MIRROR);
      Serial.println(ctrl.mode == hc::MIRROR ? F("MIRROR") : F("DEMO"));
    }
  }
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < hc::N; ++i) pinMode(POT_PIN[i], INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  EEPROM.get(0, ctrl.set);
  if (!hc::settingsOk(ctrl.set)) {
    ctrl.set = hc::defaults();
    Serial.println(F("no saved settings: DEFAULTS (servos centred; set LIM and calibrate the glove)"));
  }
  ctrl.begin();
  for (uint8_t i = 0; i < hc::N; ++i) {
    servo[i].writeMicroseconds(ctrl.us[i]);   // first pulse = open pose, no jump at attach
    servo[i].attach(SERVO_PIN[i], hc::US_MIN, hc::US_MAX);
  }
  Serial.println(F("bionic hand ready. HELP lists commands."));
}

void loop() {
  static uint32_t last = 0, lastPrint = 0;
  const uint32_t now = millis();
  pollSerial();
  pollButton(now);
  if (now - last < TICK_MS) return;
  last = now;

  readPots(raw, 4);
  const bool wasUnplugged = ctrl.unplugged;
  ctrl.update(raw, now);
  for (uint8_t i = 0; i < hc::N; ++i) servo[i].writeMicroseconds(ctrl.us[i]);
  if (ctrl.unplugged != wasUnplugged)
    Serial.println(ctrl.unplugged ? F("glove UNPLUGGED: holding, then opening") : F("glove connected"));

  digitalWrite(LED_PIN, calStep ? (now / 250) & 1 : ctrl.unplugged ? (now / 100) & 1 : ctrl.mode == hc::MIRROR);
  if (stream && now - lastPrint >= 200) {
    lastPrint = now;
    for (uint8_t i = 0; i < hc::N; ++i) {
      Serial.print(raw[i]);
      Serial.print(i + 1 < hc::N ? '\t' : '\n');
    }
  }
}

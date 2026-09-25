// Configuration commands on the USB serial port (and the web page's command box).
//   HELP | LIST | GET <name> | SET <name> <value> | SAVE PARAMS | DEFAULTS
//   PULSE <1..6> <us> | PULSE OFF      raw servo pulse for calibration (servo power on, arm idle)
//   MARK <1..6> <angle>                "the joint is now at this angle" (two marks = calibrated)
//   I2C | PAIR | REBOOT
#include <Bluepad32.h>
#include <Wire.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "storage.h"

struct Mark {
  bool set = false;
  float angle = 0, us = 0;
};
static Mark marks[NJ];

static const char* kHelp =
    "motion: ON OFF HOME STOP MODE JOINT|CART SPEED 1..3 J n deg JOINTS a b c d e [g] MOVE x y z [pitch [roll]] "
    "MOVEBY dx dy dz UP DOWN LEFT RIGHT FORWARD BACK [mm] TURN deg PITCH deg ROLL deg GRIP 0..100|OPEN|CLOSE "
    "REC DELETE CLEAR PLAY [LOOP] SAVE STATUS\n"
    "config: LIST GET name SET name value SAVE PARAMS DEFAULTS PULSE j us|OFF MARK j angle I2C PAIR REBOOT";

float jointPulse(int j) {
  if (rawUs[j] > 0) return rawUs[j];
  const float us = P.us0[j] + P.usdeg[j] * core.q[j];
  return fminf(fmaxf(us, P.pulse_min), P.pulse_max);
}

bool cliCommand(char* line, char* reply, size_t n) {
  char* argv[4];
  int argc = 0;
  for (char* tok = strtok(line, " \t"); tok && argc < 4; tok = strtok(nullptr, " \t")) argv[argc++] = tok;
  reply[0] = '\0';
  if (argc == 0) return true;
  for (int i = 0; i < argc; ++i)
    for (char* p = argv[i]; *p; ++p) *p = (char)tolower((unsigned char)*p);
  const char* cmd = argv[0];
  auto is = [&](const char* w) { return strcmp(cmd, w) == 0; };

  if (is("help") || is("?")) {
    snprintf(reply, n, "%s", kHelp);
  } else if (is("list")) {
    size_t cnt;
    const ParamInfo* t = paramTable(cnt);
    for (size_t i = 0; i < cnt; ++i) Serial.printf("%-14s %g\n", t[i].name, *paramPtr(P, t[i]));
    snprintf(reply, n, "%u parameters", (unsigned)cnt);
  } else if (is("get") && argc == 2) {
    const ParamInfo* pi = paramFind(argv[1]);
    if (pi) snprintf(reply, n, "%s = %g", pi->name, *paramPtr(P, *pi));
    else snprintf(reply, n, "error: unknown parameter %s", argv[1]);
  } else if (is("set") && argc == 3) {
    const float v = strtof(argv[2], nullptr);
    if (paramSet(P, argv[1], v)) snprintf(reply, n, "%s = %g (SAVE PARAMS to keep it)", argv[1], v);
    else snprintf(reply, n, "error: unknown parameter or value out of range");
  } else if (is("save") && argc == 2 && !strcmp(argv[1], "params")) {
    snprintf(reply, n, saveParams(P) ? "parameters saved" : "error: flash write failed");
  } else if (is("defaults")) {
    if (core.power) {
      snprintf(reply, n, "error: turn servo power OFF first");
    } else {
      paramsDefaults(P);
      core.reset();
      snprintf(reply, n, "defaults loaded (not saved; SAVE PARAMS to keep them)");
    }
  } else if (is("pulse") && argc == 2 && !strcmp(argv[1], "off")) {
    for (int j = 0; j < NJ; ++j) {
      if (rawUs[j] <= 0) continue;
      if (fabsf(P.usdeg[j]) > 1e-3f) core.syncJoint(j, (rawUs[j] - P.us0[j]) / P.usdeg[j]);
      rawUs[j] = 0;
    }
    snprintf(reply, n, "raw pulses off, joints follow the angle commands again");
  } else if (is("pulse") && argc == 3) {
    const int j = atoi(argv[1]) - 1;
    const float us = strtof(argv[2], nullptr);
    if (j < 0 || j >= NJ || us < 400 || us > 2700) {
      snprintf(reply, n, "error: PULSE 1..6 400..2700");
    } else if (core.state != arm::ST_ON || core.activity != arm::ACT_IDLE) {
      snprintf(reply, n, "error: servo power must be ON and the arm idle");
    } else {
      rawUs[j] = us;
      snprintf(reply, n, "J%d raw %.0f us (hands off the sticks; PULSE OFF when done)", j + 1, us);
    }
  } else if (is("mark") && argc == 3) {
    const int j = atoi(argv[1]) - 1;
    const float a = strtof(argv[2], nullptr);
    if (j < 0 || j >= NJ) {
      snprintf(reply, n, "error: MARK 1..6 angle");
    } else {
      const float us = jointPulse(j);
      Mark& m = marks[j];
      if (m.set && fabsf(a - m.angle) >= 20.0f) {
        const float k = (us - m.us) / (a - m.angle);
        if (fabsf(k) < 2.0f || fabsf(k) > 40.0f) {
          snprintf(reply, n, "error: %.2f us/deg is not plausible, check the two poses", k);
        } else {
          P.usdeg[j] = k;
          P.us0[j] = us - k * a;
          if (rawUs[j] > 0) core.syncJoint(j, a);
          snprintf(reply, n, "J%d calibrated: j%d_us0 = %.1f, j%d_usdeg = %.3f (SAVE PARAMS to keep it)", j + 1,
                   j + 1, P.us0[j], j + 1, P.usdeg[j]);
        }
      } else {
        snprintf(reply, n, "J%d mark: %.1f deg at %.0f us. Now move it >= 20 deg away and MARK again", j + 1, a, us);
      }
      m.set = true;
      m.angle = a;
      m.us = us;
    }
  } else if (is("i2c")) {
    int k = snprintf(reply, n, "I2C devices:");
    for (uint8_t a = 1; a < 127; ++a) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0 && k > 0 && (size_t)k < n) k += snprintf(reply + k, n - k, " 0x%02X", a);
    }
    if (k > 0 && (size_t)k < n) snprintf(reply + k, n - k, "  (want 0x40 INA226, 0x41 PCA9685)");
  } else if (is("pair")) {
    BP32.forgetBluetoothKeys();
    BP32.enableNewBluetoothConnections(true);
    snprintf(reply, n, "paired gamepads forgotten; put the G7 Pro in pairing mode now");
  } else if (is("reboot")) {
    Serial.println("rebooting");
    delay(100);
    ESP.restart();
  } else {
    return false;
  }
  return true;
}

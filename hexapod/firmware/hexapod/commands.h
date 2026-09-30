// Text commands on the USB serial port (115200 baud, one per line, upper or lower case).
// Units: mm, degrees, microseconds; legs 0..5 = LF LM LR RF RM RR, joints 0..2 = coxa femur tibia.
//
// Use:        STAND | SIT | OFF | WALK x y turn [ms] (percent, default 2000 ms) | STOP
//             GAIT TRIPOD|RIPPLE|WAVE | STATUS | HELP
// Calibrate:  CAL (all joints to 0 = calibration pose, servos on) | JOINT leg joint deg
//             TRIM leg joint us | DIR leg joint 1|-1 | CH leg joint channel | CHMAP c0 .. c17
//             BACKEND AUTO|PCA9685|GPIO | SCAN (I2C scan)
// Geometry:   GEO l1 l2 l3 | MOUNT leg x y angle | HEIGHT mm | REACH mm | LIFT mm | STRIDE mm
// Other:      LINK id | OBST mm | SHOW | SAVE | DEFAULTS
#pragma once
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "robot.h"

namespace hx {

// Where replies go (Serial on the robot, a buffer in the tests).
class Out {
 public:
  virtual void write(const char* s) = 0;
  virtual ~Out() {}
  void printf(const char* fmt, ...) {
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    write(b);
  }
};

// Things only the sketch can do.
struct Hooks {
  bool (*save)(const Params& p) = nullptr;
  void (*scan)(Out& o) = nullptr;
};

enum CmdResult { CMD_OK, CMD_ERROR, CMD_UNKNOWN };

inline bool eq(const char* a, const char* b) {
  while (*a && *b)
    if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++)) return false;
  return *a == 0 && *b == 0;
}

inline void showStatus(const Robot& r, Out& o) {
  static const char* names[] = {"RELAXED", "STANDING_UP", "READY", "SITTING_DOWN"};
  o.printf("%s%s gait %s | cmd %d %d %d %% | height %.0f tilt %.0f %.0f | vbat %.2f V%s%s%s\n",
           names[r.state & 3], r.cal ? " (CAL)" : "", gaitName(r.gait.nextGait), (int)(r.cx * 100),
           (int)(r.cy * 100), (int)(r.ct * 100), r.height, r.pitch, r.roll, r.vbatMv / 1000.0f,
           r.lowBattery ? " LOW-BATTERY" : "", r.obstacle ? " OBSTACLE" : "", r.limited ? " LIMIT" : "");
  for (int i = 0; i < NLEG; ++i) {
    static const char* legs[] = {"LF", "LM", "LR", "RF", "RM", "RR"};
    o.printf("  %s q %6.1f %6.1f %6.1f  us %4d %4d %4d\n", legs[i], r.q[i * 3], r.q[i * 3 + 1],
             r.q[i * 3 + 2], (int)r.us[i * 3], (int)r.us[i * 3 + 1], (int)r.us[i * 3 + 2]);
  }
}

inline void showParams(const Params& p, Out& o) {
  static const char* bk[] = {"AUTO", "PCA9685", "GPIO"};
  o.printf("GEO %.1f %.1f %.1f | HEIGHT %.0f REACH %.0f LIFT %.0f STRIDE %.0f | GAIT %s\n", p.l1, p.l2,
           p.l3, p.height, p.reach, p.lift, p.stride, gaitName(p.gait));
  for (int i = 0; i < NLEG; ++i)
    o.printf("MOUNT %d %.1f %.1f %.1f\n", i, p.mount[i][0], p.mount[i][1], p.mount[i][2]);
  o.printf("BACKEND %s | CHMAP", bk[p.backend % 3]);
  for (int s = 0; s < NSERVO; ++s) o.printf(" %d", p.ch[s]);
  o.printf("\nTRIM");
  for (int s = 0; s < NSERVO; ++s) o.printf(" %d", p.trim[s]);
  o.printf("\nDIR ");
  for (int s = 0; s < NSERVO; ++s) o.printf(" %d", p.dir[s]);
  o.printf("\nLINK %d | OBST %d | vbat warn %.1f cut %.1f V\n", p.link_id, p.obstacle_mm,
           p.vbat_warn_mv / 1000.0f, p.vbat_cut_mv / 1000.0f);
}

// Parses and runs one line. `now` = millis().
inline CmdResult runCommand(char* line, Robot& r, Params& p, const Hooks& hooks, Out& o, uint32_t now) {
  char* tok[20];
  int n = 0;
  for (char* t = strtok(line, " \t\r\n"); t && n < 20; t = strtok(nullptr, " \t\r\n")) tok[n++] = t;
  if (n == 0) return CMD_OK;
  const char* c = tok[0];
  float a[19];
  for (int i = 1; i < n; ++i) a[i - 1] = (float)atof(tok[i]);
  const int na = n - 1;
  auto bad = [&](const char* why) {
    o.printf("error: %s\n", why);
    return CMD_ERROR;
  };
  auto ok = [&]() {
    o.printf("ok\n");
    return CMD_OK;
  };
  auto legJoint = [&](int& s) {
    const int leg = (int)a[0], j = (int)a[1];
    if (leg < 0 || leg >= NLEG || j < 0 || j > 2) return false;
    s = leg * 3 + j;
    return true;
  };
  int s = 0;

  if (eq(c, "HELP")) {
    o.printf("STAND SIT OFF STOP | WALK x y turn [ms] | GAIT TRIPOD|RIPPLE|WAVE | STATUS\n"
             "CAL | JOINT leg j deg | TRIM leg j us | DIR leg j 1|-1 | CH leg j ch | CHMAP c0..c17\n"
             "BACKEND AUTO|PCA9685|GPIO | SCAN | GEO l1 l2 l3 | MOUNT leg x y ang\n"
             "HEIGHT | REACH | LIFT | STRIDE mm | LINK id | OBST mm | SHOW | SAVE | DEFAULTS\n");
    return CMD_OK;
  }
  if (eq(c, "STAND")) {
    if (r.lowBattery) return bad("battery low");
    r.standUp();
    return ok();
  }
  if (eq(c, "SIT")) {
    r.sitDown();
    return ok();
  }
  if (eq(c, "OFF")) {
    r.relax();
    return ok();
  }
  if (eq(c, "STOP")) {
    r.setInput(Input(), now, 0);
    return ok();
  }
  if (eq(c, "WALK")) {
    if (na < 3) return bad("WALK x y turn [ms]");
    if (r.state != RS_READY) return bad("STAND first");
    Input in;
    in.mode = MODE_WALK;
    in.x = clampf(a[0] / 100, -1, 1);
    in.y = clampf(a[1] / 100, -1, 1);
    in.turn = clampf(a[2] / 100, -1, 1);
    r.setInput(in, now, na >= 4 ? (uint32_t)a[3] : 2000);
    return ok();
  }
  if (eq(c, "GAIT")) {
    if (n < 2) return bad("GAIT TRIPOD|RIPPLE|WAVE");
    for (uint8_t g = 0; g < GAIT_COUNT; ++g)
      if (eq(tok[1], gaitName(g))) {
        r.setGait(g);
        p.gait = g;
        return ok();
      }
    return bad("GAIT TRIPOD|RIPPLE|WAVE");
  }
  if (eq(c, "STATUS")) {
    showStatus(r, o);
    return CMD_OK;
  }
  if (eq(c, "CAL")) {
    r.calibrate();
    return ok();
  }
  if (eq(c, "JOINT")) {
    if (na < 3 || !legJoint(s)) return bad("JOINT leg(0-5) joint(0-2) deg");
    if (!r.cal) return bad("CAL first");
    r.calQ[s] = clampf(a[2], -90, 90);
    return ok();
  }
  if (eq(c, "TRIM")) {
    if (na < 3 || !legJoint(s)) return bad("TRIM leg joint us");
    p.trim[s] = (int16_t)clampf(a[2], -400, 400);
    return ok();
  }
  if (eq(c, "DIR")) {
    if (na < 3 || !legJoint(s) || (a[2] != 1 && a[2] != -1)) return bad("DIR leg joint 1|-1");
    p.dir[s] = (int8_t)a[2];
    return ok();
  }
  if (eq(c, "CH")) {
    if (na < 3 || !legJoint(s) || a[2] < 0 || a[2] > 39) return bad("CH leg joint channel");
    p.ch[s] = (uint8_t)a[2];
    o.printf("ok (restart to apply)\n");
    return CMD_OK;
  }
  if (eq(c, "CHMAP")) {
    if (na != NSERVO) return bad("CHMAP needs 18 numbers: LF coxa femur tibia, LM .., LR .., RF .., RM .., RR ..");
    for (int i = 0; i < NSERVO; ++i)
      if (a[i] < 0 || a[i] > 39) return bad("channel 0..39");
    for (int i = 0; i < NSERVO; ++i) p.ch[i] = (uint8_t)a[i];
    o.printf("ok (SAVE, then restart to apply)\n");
    return CMD_OK;
  }
  if (eq(c, "BACKEND")) {
    static const char* bk[] = {"AUTO", "PCA9685", "GPIO"};
    for (uint8_t b = 0; b < 3 && n >= 2; ++b)
      if (eq(tok[1], bk[b])) {
        p.backend = b;
        o.printf("ok (SAVE, then restart to apply)\n");
        return CMD_OK;
      }
    return bad("BACKEND AUTO|PCA9685|GPIO");
  }
  if (eq(c, "SCAN")) {
    if (hooks.scan) hooks.scan(o);
    return CMD_OK;
  }
  if (eq(c, "GEO")) {
    if (na < 3 || a[0] < 0 || a[1] < 10 || a[2] < 10) return bad("GEO l1 l2 l3 (mm)");
    p.l1 = a[0];
    p.l2 = a[1];
    p.l3 = a[2];
    return ok();
  }
  if (eq(c, "MOUNT")) {
    if (na < 4 || a[0] < 0 || a[0] >= NLEG) return bad("MOUNT leg x y angle");
    for (int k = 0; k < 3; ++k) p.mount[(int)a[0]][k] = a[k + 1];
    return ok();
  }
  struct Num {
    const char* name;
    float* v;
    float lo, hi;
  } nums[] = {{"HEIGHT", &p.height, 20, 150}, {"REACH", &p.reach, 20, 250},
              {"LIFT", &p.lift, 5, 80},       {"STRIDE", &p.stride, 5, 120}};
  for (const Num& m : nums)
    if (eq(c, m.name)) {
      if (na < 1 || a[0] < m.lo || a[0] > m.hi) return bad("value out of range");
      *m.v = a[0];
      return ok();
    }
  if (eq(c, "LINK")) {
    if (na < 1 || a[0] < 0 || a[0] > 255) return bad("LINK 0..255 (same on the glove)");
    p.link_id = (uint8_t)a[0];
    return ok();
  }
  if (eq(c, "OBST")) {
    if (na < 1 || a[0] < 0 || a[0] > 2000) return bad("OBST mm (0 = off)");
    p.obstacle_mm = (uint16_t)a[0];
    return ok();
  }
  if (eq(c, "SHOW")) {
    showParams(p, o);
    return CMD_OK;
  }
  if (eq(c, "SAVE")) {
    if (!hooks.save || !hooks.save(p)) return bad("save failed");
    return ok();
  }
  if (eq(c, "DEFAULTS")) {
    paramsDefaults(p);
    r.relax();
    return ok();
  }
  o.printf("error: unknown command (HELP)\n");
  return CMD_UNKNOWN;
}

}  // namespace hx

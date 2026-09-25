// USB serial command line + log streaming. Type "help" in the Serial Monitor (115200, newline).
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "gps.h"
#include "shared.h"

static const char* modeName(uint8_t m) {
  switch (m) {
    case proto::MODE_MANUAL: return "MANUAL";
    case proto::MODE_STABILIZE: return "STABILIZE";
    case proto::MODE_HEADING_HOLD: return "HEADING_HOLD";
    case proto::MODE_AUTO: return "AUTO";
    case proto::MODE_RTH: return "RTH";
    default: return "?";
  }
}

static const char* stateName(uint8_t s) {
  switch (s) {
    case proto::ST_DISARMED: return "DISARMED";
    case proto::ST_ARMED: return "ARMED";
    case proto::ST_FAILSAFE: return "FAILSAFE";
    default: return "?";
  }
}

static Snapshot snap() {
  portENTER_CRITICAL(&gMux);
  Snapshot s = gSnap;
  portEXIT_CRITICAL(&gMux);
  return s;
}

static void printHelp() {
  Serial.println(F(
      "Commands:\n"
      "  status                     overall state\n"
      "  imu                        one line of IMU data (body frame FRD)\n"
      "  calib                      gyro calibration (disarmed, keep still)\n"
      "  list | get <n> | set <n> <v> | save | defaults\n"
      "  servo <L_deg> <R_deg>      move wings (disarmed only)   servo off\n"
      "  bench <thr 0..1> [mode 0|1|2]   flap on the bench without radio\n"
      "  bench stick <roll> <pitch> <yaw>  (-1..1)             bench off\n"
      "  log off|att|fft|raw        stream CSV (att 50 Hz, fft 200 Hz, raw 1 kHz)\n"
      "  gps                        GPS status and position"));
}

static void printStatus() {
  Snapshot s = snap();
  Serial.printf("%s | state %s | mode %s | imu %s (err %lu) | vbat %.2f V%s\n", FW_VERSION,
                stateName(s.out.state), modeName(s.out.mode),
                s.imuPresent ? (s.imuOk ? "OK" : "FAULT") : "MISSING",
                (unsigned long)s.imuErrors, gVbat, s.in.charging ? " | CHARGING (arming locked)" : "");
  if (s.out.rth) Serial.println("RETURNING HOME");
  Serial.printf("link %s | thr %.2f roll %.2f pitch %.2f yaw %.2f | armReq %d%s | flap %.2f Hz\n",
                s.in.linkOk ? "OK" : "LOST", s.in.thr, s.in.roll, s.in.pitch, s.in.yaw,
                s.in.armReq, s.out.armBlocked ? " (BLOCKED: flip arm switch off, throttle low)" : "",
                s.out.flapHz);
  Serial.printf("att roll %.1f pitch %.1f yaw %.1f | avg roll %.1f pitch %.1f | loop max %lu us\n",
                s.roll, s.pitch, s.yaw, s.rollAvg, s.pitchAvg, (unsigned long)s.loopMaxUs);
  Serial.printf("baro %s | alt %.2f m (target %.2f) | vz %+.2f m/s | thr cmd %.2f\n",
                s.baroPresent ? (s.baroOk ? "OK" : "FAULT") : "MISSING",
                s.alt, s.out.altTarget, s.vz, s.out.thrCmd);
}

static void printImu() {
  Snapshot s = snap();
  Serial.printf("acc[g] x %+.2f y %+.2f z %+.2f | gyro[dps] p %+7.1f q %+7.1f r %+7.1f | "
                "roll %+6.1f pitch %+6.1f yaw %+6.1f | alt %+.2f m\n",
                s.acc[0], s.acc[1], s.acc[2], s.gyro[0], s.gyro[1], s.gyro[2],
                s.roll, s.pitch, s.yaw, s.alt);
}

static void listParams() {
  size_t n;
  const ParamInfo* t = paramTable(n);
  for (size_t i = 0; i < n; ++i) {
    float v;
    paramGet(t[i].name, v);
    Serial.printf("%-18s %10.4f   [%g .. %g]\n", t[i].name, v, t[i].minV, t[i].maxV);
  }
}

static bool isDisarmed() { return snap().out.state == proto::ST_DISARMED; }

static void handle(char* line) {
  char* argv[6];
  int argc = 0;
  for (char* tok = strtok(line, " \t"); tok && argc < 6; tok = strtok(nullptr, " \t")) argv[argc++] = tok;
  if (argc == 0) return;
  const char* cmd = argv[0];

  if (!strcmp(cmd, "help")) {
    printHelp();
  } else if (!strcmp(cmd, "status")) {
    printStatus();
  } else if (!strcmp(cmd, "imu")) {
    printImu();
  } else if (!strcmp(cmd, "gps")) {
    const GpsFix& g = gpsFix();
    if (gpsBaud() == 0) {
      Serial.println("GPS: searching (no NMEA at 115200/38400/9600/57600 yet) - check TX->D7 and power");
    } else {
      Serial.printf("GPS: %lu baud, %lu sentences | %s | sats %u hdop %.1f\n", (unsigned long)gpsBaud(),
                    (unsigned long)gpsSentences(), gpsFresh() ? "FIX" : "no fix (needs open sky)", g.sats, g.hdop);
      if (gpsFresh())
        Serial.printf("lat %.7f lon %.7f alt %.1f m | speed %.1f m/s course %.0f deg\n",
                      g.lat, g.lon, g.altMsl, g.speed, g.course);
      portENTER_CRITICAL(&gMux);
      const GpsNav n = gGpsNav;
      portEXIT_CRITICAL(&gMux);
      const Snapshot s = snap();
      Serial.printf("home %s | distance %.0f m | north aligned %s | RTH %s (rth_enable %.0f)\n",
                    n.homeSet ? "set" : "not set (set when arming with a fix)",
                    sqrtf(n.north * n.north + n.east * n.east), s.northValid ? "yes" : "no (fly straight > 1.5 m/s)",
                    s.out.rth ? "ACTIVE" : "idle", P.rth_enable);
    }
  } else if (!strcmp(cmd, "calib")) {
    if (!isDisarmed()) { Serial.println("refused: disarm first"); return; }
    gCalibRequest = true;
    Serial.println("calibrating gyro, keep still ...");
  } else if (!strcmp(cmd, "list")) {
    listParams();
  } else if (!strcmp(cmd, "get") && argc == 2) {
    float v;
    if (paramGet(argv[1], v)) Serial.printf("%s = %.4f\n", argv[1], v);
    else Serial.println("unknown parameter");
  } else if (!strcmp(cmd, "set") && argc == 3) {
    if (paramSet(argv[1], atof(argv[2]))) Serial.printf("%s = %s  (not saved yet)\n", argv[1], argv[2]);
    else Serial.println("unknown parameter or out of range (see: list)");
  } else if (!strcmp(cmd, "save")) {
    gSaveRequest = true;
  } else if (!strcmp(cmd, "defaults")) {
    paramsReset();
    Serial.println("defaults loaded (not saved yet)");
  } else if (!strcmp(cmd, "servo") && argc >= 2) {
    if (!strcmp(argv[1], "off")) { gServoTest = false; Serial.println("servo test off"); return; }
    if (argc != 3) { Serial.println("usage: servo <L_deg> <R_deg>"); return; }
    if (!isDisarmed()) { Serial.println("refused: disarm first"); return; }
    portENTER_CRITICAL(&gMux);
    gServoTestL = atof(argv[1]);
    gServoTestR = atof(argv[2]);
    portEXIT_CRITICAL(&gMux);
    gServoTest = true;
    Serial.printf("wings L %.1f R %.1f deg\n", gServoTestL, gServoTestR);
  } else if (!strcmp(cmd, "bench") && argc >= 2) {
    portENTER_CRITICAL(&gMux);
    if (!strcmp(argv[1], "off")) {
      gBench = BenchState();
    } else if (!strcmp(argv[1], "stick") && argc == 5) {
      gBench.roll = constrain(atof(argv[2]), -1.0, 1.0);
      gBench.pitch = constrain(atof(argv[3]), -1.0, 1.0);
      gBench.yaw = constrain(atof(argv[4]), -1.0, 1.0);
    } else {
      gBench.thr = constrain(atof(argv[1]), 0.0, 1.0);
      if (argc >= 3) gBench.mode = (uint8_t)constrain(atoi(argv[2]), 0, 2);   // no AUTO on the bench
      if (!gBench.active) gBench.startMs = millis();
      gBench.active = true;
      gServoTest = false;
    }
    BenchState b = gBench;
    portEXIT_CRITICAL(&gMux);
    Serial.printf("bench %s thr %.2f mode %s stick %.2f %.2f %.2f (auto-off after 120 s)\n",
                  b.active ? "ON" : "OFF", b.thr, modeName(b.mode), b.roll, b.pitch, b.yaw);
  } else if (!strcmp(cmd, "log") && argc == 2) {
    if (!strcmp(argv[1], "att")) gLogMode = LOG_ATT;
    else if (!strcmp(argv[1], "fft")) gLogMode = LOG_FFT;
    else if (!strcmp(argv[1], "raw")) gLogMode = LOG_RAW;
    else gLogMode = LOG_OFF;
    if (gLogMode == LOG_ATT) Serial.println("# A,t_ms,roll,pitch,yaw,roll_avg,pitch_avg,ur,up,uy,thr_cmd,flap_hz,alt,vz");
    if (gLogMode == LOG_FFT) Serial.println("# F,t_us,p_dec,q_dec,r_dec,p_notch,q_notch,r_notch,flap_hz");
    if (gLogMode == LOG_RAW) Serial.println("# R,t_us,p_raw,q_raw,r_raw");
  } else {
    Serial.println("unknown command, type: help");
  }
}

void cliPoll() {
  static char buf[96];
  static size_t len = 0;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[len] = '\0';
      handle(buf);
      len = 0;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

void cliDrainLog() {
  LogRec r;
  int budget = 64;   // bound time spent per loop() pass
  while (budget-- > 0 && logPop(r)) {
    Serial.printf("%c,%lu", r.kind, (unsigned long)r.t);
    for (int i = 0; i < r.n; ++i) Serial.printf(",%.3f", r.v[i]);
    Serial.print('\n');
  }
}

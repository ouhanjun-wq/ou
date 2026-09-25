// Bionic butterfly flight controller
// Board: Seeed Studio XIAO ESP32S3 (Arduino-ESP32 core 3.x, "USB CDC On Boot: Enabled")
// IMU:   ICM-42688-P on SPI      Servos: 2 x wing servo      Radio: ESP-NOW
//
// Core 1: controlTask (1 kHz IMU + attitude, 200 Hz control) and loop() (CLI, telemetry)
// Core 0: Wi-Fi / ESP-NOW receive callback
#include <SPI.h>

#include "ahrs.h"
#include "bmp280.h"
#include "config.h"
#include "filters.h"
#include "flight.h"
#include "gps.h"
#include "icm42688.h"
#include "params.h"
#include "servo_out.h"
#include "shared.h"

// ---------------- shared state (declared in shared.h) ----------------
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
FlightInputs gRadioIn;
uint32_t gLastRadioMs = 0;
volatile uint32_t gRadioPktCount = 0;
float gPendingTurn = 0;
float gPendingAlt = 0;
GpsNav gGpsNav;
volatile bool gCalibRequest = false;
volatile bool gSaveRequest = false;
Snapshot gSnap;
float gVbat = 0;
BenchState gBench;
volatile bool gServoTest = false;
float gServoTestL = 0, gServoTestR = 0;
volatile uint8_t gLogMode = LOG_OFF;
volatile uint32_t gLogDropped = 0;

static constexpr int LOG_CAP = 512;
static LogRec logBuf[LOG_CAP];
static volatile uint32_t logHead = 0, logTail = 0;

bool logPush(const LogRec& r) {
  const uint32_t next = (logHead + 1) % LOG_CAP;
  if (next == logTail) { gLogDropped = gLogDropped + 1; return false; }
  logBuf[logHead] = r;
  logHead = next;
  return true;
}

bool logPop(LogRec& r) {
  if (logTail == logHead) return false;
  r = logBuf[logTail];
  logTail = (logTail + 1) % LOG_CAP;
  return true;
}

// ---------------- GPS (optional, UART, NMEA) ----------------
// Auto-detects the module's baud rate, then parses GGA / RMC in loop().
static NmeaParser gps;
static uint32_t gpsBaudRate = 0, gpsTryStart = 0, gpsLastSentenceMs = 0, gpsSentencesAtTry = 0;
static int gpsTryIndex = -1;
static const uint32_t kGpsBauds[] = {115200, 38400, 9600, 57600};

const GpsFix& gpsFix() { return gps.fix(); }
bool gpsFresh() { return gps.fix().valid && millis() - gps.fix().updatedMs < 2000; }
uint32_t gpsBaud() { return gpsBaudRate; }
uint32_t gpsSentences() { return gps.sentences(); }

static void gpsPoll() {
  const uint32_t now = millis();
  while (Serial1.available()) {
    if (gps.feed((char)Serial1.read(), now)) gpsLastSentenceMs = now;
  }
  if (gpsBaudRate != 0) {
    if (now - gpsLastSentenceMs > 5000) { gpsBaudRate = 0; gpsTryIndex = -1; }   // lost: search again
    return;
  }
  if (gpsTryIndex >= 0 && gps.sentences() > gpsSentencesAtTry + 2) {             // locked on
    gpsBaudRate = kGpsBauds[gpsTryIndex];
    gpsLastSentenceMs = now;
    return;
  }
  if (gpsTryIndex < 0 || now - gpsTryStart > 1500) {
    gpsTryIndex = (gpsTryIndex + 1) % (int)(sizeof(kGpsBauds) / sizeof(kGpsBauds[0]));
    Serial1.end();
    Serial1.begin(kGpsBauds[gpsTryIndex], SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    gpsTryStart = now;
    gpsSentencesAtTry = gps.sentences();
  }
}

// Home = position when the butterfly arms (or the first fix while armed). Publishes
// position relative to home for the control task.
static double homeLat = 0, homeLon = 0;
static bool homeSet = false;

static void navUpdate(uint8_t state) {
  static uint8_t prevState = proto::ST_DISARMED;
  const bool fresh = gpsFresh();
  const GpsFix& g = gps.fix();
  const bool armed = state != proto::ST_DISARMED;
  if (fresh && armed && (prevState == proto::ST_DISARMED || !homeSet)) {
    homeLat = g.lat;
    homeLon = g.lon;
    homeSet = true;
  }
  prevState = state;
  GpsNav n;
  n.ok = fresh;
  n.homeSet = homeSet;
  if (fresh && homeSet) {
    n.north = (float)((g.lat - homeLat) * 111320.0);
    n.east = (float)((g.lon - homeLon) * 111320.0 * cos(homeLat * 0.017453292519943));
  }
  n.course = g.course;
  n.speed = g.speed;
  portENTER_CRITICAL(&gMux);
  gGpsNav = n;
  portEXIT_CRITICAL(&gMux);
}

// ---------------- control-task objects ----------------
static ICM42688 imu;
static BMP280 baro;
static bool baroPresent = false;
static Mahony ahrs;
static FlightCore core;
static ServoOut servos;
static bool imuPresent = false;
static float gyroBias[3] = {0, 0, 0};

// Chip frame (x fwd, y left, z up when mounted flat, marking up) -> body FRD.
static void toBody(const float c[3], float b[3]) {
  float x = c[0], y = c[1], z = c[2];
  const int n = (int)P.imu_yaw_quarter & 3;   // board turned clockwise by n x 90 deg
  for (int i = 0; i < n; ++i) { const float t = x; x = y; y = -t; }
  if (P.imu_flip > 0.5f) { b[0] = x; b[1] = y;  b[2] = z; }   // upside down
  else                   { b[0] = x; b[1] = -y; b[2] = -z; }
}

// Average 1 s of gyro while still. Retries up to 5 times if the board moves.
static bool calibrateGyro() {
  for (int attempt = 0; attempt < 5; ++attempt) {
    float sum[3] = {0, 0, 0}, lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9};
    float accSum[3] = {0, 0, 0};
    int n = 0;
    for (int i = 0; i < 1000; ++i) {
      float ac[3], gc[3], ab[3], gb[3];
      if (imu.read(ac, gc)) {
        toBody(gc, gb);
        toBody(ac, ab);
        for (int k = 0; k < 3; ++k) {
          sum[k] += gb[k]; accSum[k] += ab[k];
          lo[k] = min(lo[k], gb[k]); hi[k] = max(hi[k], gb[k]);
        }
        ++n;
      }
      vTaskDelay(1);
    }
    if (n < 500) continue;
    const bool still = hi[0] - lo[0] < 4 && hi[1] - lo[1] < 4 && hi[2] - lo[2] < 4;
    if (still) {
      float acc[3];
      for (int k = 0; k < 3; ++k) { gyroBias[k] = sum[k] / n; acc[k] = accSum[k] / n; }
      ahrs.initFromAccel(acc);
      return true;
    }
  }
  return false;
}

static void controlTask(void*) {
  PT1 gLpf[3], aLpf[3];
  Decimator dec[3];
  Notch n1[3], n2[3];
  static StrokeAvg<STROKE_MAX_N> sRoll, sPitch, sAlt;
  PT1 altLpf, vzLpf, pRefLpf;
  float altRaw = 0, pRef = 0, prevAlt = 0, alt = 0, vz = 0;
  int baroDiv = 0, baroBad = 0;
  uint8_t prevState = proto::ST_DISARMED;
  FlightOutput lastOut;
  float lastNotchF = -1;
  int badReads = 0, attDiv = 0;
  bool charging = digitalRead(PIN_CHG_DETECT) == HIGH;
  int chgCount = 0;
  float acc[3] = {0, 0, -1}, gyroRaw[3] = {0, 0, 0}, gF[3] = {0, 0, 0}, aF[3] = {0, 0, -1};
  uint32_t lastUs = micros(), loopMaxUs = 0;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&lastWake, 1);   // 1 kHz (FreeRTOS tick = 1 ms)
    const uint32_t t0 = micros();
    const float dt = constrain((t0 - lastUs) * 1e-6f, 0.0002f, 0.01f);
    lastUs = t0;

    if (gParamsDirty) {
      gParamsDirty = false;
      for (int k = 0; k < 3; ++k) {
        gLpf[k].setCutoff(P.gyro_lpf_hz, IMU_HZ);
        aLpf[k].setCutoff(P.acc_lpf_hz, IMU_HZ);
      }
      core.reinit(P, CTRL_HZ);
      altLpf.setCutoff(P.baro_lpf_hz, CTRL_HZ);
      vzLpf.setCutoff(1.0f, CTRL_HZ);
      pRefLpf.setCutoff(0.5f, CTRL_HZ / 10);
      lastNotchF = -1;
    }

    if (gCalibRequest) {
      gCalibRequest = false;
      if (imuPresent && core.state() == proto::ST_DISARMED) {
        const bool ok = calibrateGyro();
        Serial.println(ok ? "gyro calibrated" : "gyro calibration FAILED: keep the board still");
        lastWake = xTaskGetTickCount();
        lastUs = micros();
      }
    }

    // ---- 1 kHz: IMU, low-pass, attitude ----
    float ac[3], gc[3];
    if (imuPresent && imu.read(ac, gc)) {
      badReads = 0;
      toBody(ac, acc);
      toBody(gc, gyroRaw);
      for (int k = 0; k < 3; ++k) gyroRaw[k] -= gyroBias[k];
    } else if (badReads < 1000) {
      ++badReads;
    }
    const bool imuOk = imuPresent && badReads < 50;

    for (int k = 0; k < 3; ++k) {
      gF[k] = gLpf[k].apply(gyroRaw[k]);
      aF[k] = aLpf[k].apply(acc[k]);
    }
    // The estimator integrates the true (un-notched) body rate, flapping motion included.
    const float d2r = 0.01745329f;
    const float gRad[3] = {gF[0] * d2r, gF[1] * d2r, gF[2] * d2r};
    if (imuOk) ahrs.update(gRad, aF, accTrust(aF[0], aF[1], aF[2]), P.ahrs_kp, P.ahrs_ki, dt);

    if (gLogMode == LOG_RAW) logPush({t0, 'R', 3, {gyroRaw[0], gyroRaw[1], gyroRaw[2]}});

    // ---- decimate 1 kHz -> 200 Hz ----
    float gD[3];
    bool ready = true;
    for (int k = 0; k < 3; ++k) ready = dec[k].push(gF[k], CTRL_DIV, gD[k]) && ready;
    if (!ready) continue;

    // ---- 200 Hz: flapping-synchronous filters ----
    const float fFlap = lastOut.flapHz;
    if (fabsf(fFlap - lastNotchF) > 0.02f) {
      const float fn = P.notch_on > 0.5f ? fFlap : 0.0f;
      for (int k = 0; k < 3; ++k) {
        n1[k].set(fn, CTRL_HZ, P.notch_q);
        n2[k].set(2.0f * fn, CTRL_HZ, P.notch_q);
      }
      const int win = fFlap > 0.5f ? (int)lroundf(CTRL_HZ / fFlap) : (int)(CTRL_HZ * 0.25f);
      sRoll.setWindow(win);
      sPitch.setWindow(win);
      sAlt.setWindow(win);
      lastNotchF = fFlap;
    }
    float gN[3];
    for (int k = 0; k < 3; ++k) gN[k] = n2[k].apply(n1[k].apply(gD[k]));
    const float rollAvg = sRoll.apply(ahrs.roll);
    const float pitchAvg = sPitch.apply(ahrs.pitch);

    // ---- barometer: 20 Hz read, stroke average + low-pass -> altitude, derivative -> climb rate ----
    if (baroPresent && ++baroDiv >= 10) {
      baroDiv = 0;
      float pa, tc;
      if (baro.read(pa, tc)) {
        baroBad = 0;
        const float pSmooth = pRefLpf.apply(pa);
        if (pRef == 0) pRef = pSmooth;
        if (lastOut.state != proto::ST_DISARMED && prevState == proto::ST_DISARMED) pRef = pSmooth;  // zero at arming
        altRaw = pressureToAltitude(pa, pRef);
      } else if (baroBad < 100) {
        ++baroBad;
      }
      prevState = lastOut.state;
    }
    const bool baroOk = baroPresent && baroBad < 10;
    alt = altLpf.apply(sAlt.apply(altRaw));
    vz = vzLpf.apply((alt - prevAlt) * CTRL_HZ);
    prevAlt = alt;

    FlightSensors s;
    s.rollAvg = P.stroke_avg_on > 0.5f ? rollAvg : ahrs.roll;
    s.pitchAvg = P.stroke_avg_on > 0.5f ? pitchAvg : ahrs.pitch;
    s.yaw = ahrs.yaw;
    s.p = gN[0]; s.q = gN[1]; s.r = gN[2];
    s.imuOk = imuOk;
    s.alt = alt;
    s.vz = vz;
    s.baroOk = baroOk;
    portENTER_CRITICAL(&gMux);
    const GpsNav nav = gGpsNav;
    portEXIT_CRITICAL(&gMux);
    s.gpsOk = nav.ok;
    s.homeSet = nav.homeSet;
    s.north = nav.north;
    s.east = nav.east;
    s.gpsCourse = nav.course;
    s.gpsSpeed = nav.speed;

    // ---- inputs: radio or USB bench ----
    FlightInputs in;
    uint32_t lastRx;
    BenchState bench;
    portENTER_CRITICAL(&gMux);
    in = gRadioIn;
    lastRx = gLastRadioMs;
    in.turnDeg = gPendingTurn;
    gPendingTurn = 0;
    in.altDelta = gPendingAlt;
    gPendingAlt = 0;
    bench = gBench;
    portEXIT_CRITICAL(&gMux);
    in.linkOk = lastRx != 0 && (millis() - lastRx) < (uint32_t)P.fs_timeout_ms;
    // Charge lock: debounced (100 ms) VBUS detect of the Type-C charging port.
    const bool chgPin = digitalRead(PIN_CHG_DETECT) == HIGH;
    if (chgPin != charging) {
      if (++chgCount >= 20) { charging = chgPin; chgCount = 0; }
    } else {
      chgCount = 0;
    }
    in.charging = charging;
    const float vb = gVbat;
    in.lowBatt = vb > 1.0f && vb < P.vcell_warn * P.cells;
    if (bench.active) {
      in.bench = true;
      in.thr = bench.thr;
      in.roll = bench.roll; in.pitch = bench.pitch; in.yaw = bench.yaw;
      in.mode = bench.mode;
    }

    const FlightOutput out = core.step(in, s, P, 1.0f / CTRL_HZ);
    lastOut = out;

    if (gServoTest && out.state == proto::ST_DISARMED) {
      portENTER_CRITICAL(&gMux);
      const float l = gServoTestL, r = gServoTestR;
      portEXIT_CRITICAL(&gMux);
      servos.writeWings(l, r, P);
    } else {
      servos.writeWings(out.wingL, out.wingR, P);
    }

    // ---- publish ----
    const uint32_t loopUs = micros() - t0;
    loopMaxUs = max(loopMaxUs, loopUs);
    portENTER_CRITICAL(&gMux);
    gSnap.roll = ahrs.roll; gSnap.pitch = ahrs.pitch; gSnap.yaw = ahrs.yaw;
    gSnap.rollAvg = rollAvg; gSnap.pitchAvg = pitchAvg;
    gSnap.alt = alt; gSnap.vz = vz;
    gSnap.baroPresent = baroPresent; gSnap.baroOk = baroOk;
    for (int k = 0; k < 3; ++k) { gSnap.gyro[k] = gF[k]; gSnap.acc[k] = aF[k]; }
    gSnap.in = in;
    gSnap.out = out;
    gSnap.imuPresent = imuPresent;
    gSnap.imuOk = imuOk;
    gSnap.northValid = core.northValid();
    gSnap.imuErrors = imu.errors();
    gSnap.loopMaxUs = loopMaxUs;
    portEXIT_CRITICAL(&gMux);

    if (gLogMode == LOG_FFT) {
      logPush({t0, 'F', 7, {gD[0], gD[1], gD[2], gN[0], gN[1], gN[2], out.flapHz}});
    } else if (gLogMode == LOG_ATT && ++attDiv >= 4) {   // 50 Hz
      attDiv = 0;
      logPush({millis(), 'A', 12, {ahrs.roll, ahrs.pitch, ahrs.yaw, rollAvg, pitchAvg,
                                   out.ur, out.up, out.uy, out.thrCmd, out.flapHz, alt, vz}});
    }
  }
}

// ---------------- setup / loop ----------------
void setup() {
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_CHG_DETECT, INPUT);   // the divider's 200k acts as pull-down; see firmware/README.md
  digitalWrite(PIN_LED, HIGH);   // off (active low)
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);      // never block when no USB host is reading
#endif
  delay(300);
  Serial.println(FW_VERSION);

  Serial.println(paramsLoad() ? "params loaded from flash" : "params: defaults");

  servos.begin(PIN_SERVO_L, PIN_SERVO_R);
  servos.writeWings(P.center, P.center, P);

  pinMode(PIN_IMU_CS, OUTPUT);
  digitalWrite(PIN_IMU_CS, HIGH);
  pinMode(PIN_BARO_CS, OUTPUT);
  digitalWrite(PIN_BARO_CS, HIGH);   // both chips deselected before the bus starts
  SPI.begin(PIN_IMU_SCK, PIN_IMU_MISO, PIN_IMU_MOSI, PIN_IMU_CS);
  imuPresent = imu.begin(SPI, PIN_IMU_CS);
  Serial.printf("IMU ICM-42688-P: %s (WHO_AM_I=0x%02X)\n", imuPresent ? "OK" : "NOT FOUND", imu.whoAmI());
  if (imuPresent) {
    Serial.println("calibrating gyro, keep still ...");
    Serial.println(calibrateGyro() ? "gyro calibrated" : "gyro calibration FAILED (moving?) - run: calib");
  }

  baroPresent = baro.begin(SPI, PIN_BARO_CS);
  Serial.printf("baro BMP280: %s (chip id 0x%02X)%s\n", baroPresent ? "OK" : "NOT FOUND", baro.chipId(),
                baroPresent ? "" : " - AUTO mode falls back to HEADING_HOLD");

  Serial.println(linkBegin() ? "ESP-NOW ready" : "ESP-NOW init FAILED");
  analogReadResolution(12);

  xTaskCreatePinnedToCore(controlTask, "control", 8192, nullptr, 5, nullptr, 1);
  Serial.println("type: help");
}

void loop() {
  static uint32_t lastTelem = 0, lastVbat = 0, lastPps = 0, ppsPrev = 0;
  static uint8_t pps = 0;
  static PT1 vbatLpf;
  const uint32_t now = millis();

  cliPoll();
  cliDrainLog();
  gpsPoll();
  portENTER_CRITICAL(&gMux);
  const uint8_t stateNow = gSnap.out.state;
  portEXIT_CRITICAL(&gMux);
  navUpdate(stateNow);

  if (now - lastVbat >= 100) {
    lastVbat = now;
    vbatLpf.setCutoff(0.5f, 10.0f);
    gVbat = vbatLpf.apply(analogReadMilliVolts(PIN_VBAT) * P.vbat_ratio / 1000.0f);
  }
  if (now - lastPps >= 1000) {
    lastPps = now;
    const uint32_t c = gRadioPktCount;
    pps = (uint8_t)min<uint32_t>(c - ppsPrev, 255);
    ppsPrev = c;
  }

  portENTER_CRITICAL(&gMux);
  const Snapshot s = gSnap;
  if (gBench.active && now - gBench.startMs > 120000) gBench = BenchState();
  portEXIT_CRITICAL(&gMux);

  const bool lowBatt = gVbat > 1.0f && gVbat < P.vcell_warn * P.cells;

  if (gSaveRequest) {
    gSaveRequest = false;
    if (s.out.state == proto::ST_DISARMED) Serial.println(paramsSave() ? "saved" : "save FAILED");
    else Serial.println("save refused: disarm first");
  }

  if (now - lastTelem >= TELEMETRY_PERIOD_MS) {
    lastTelem = now;
    proto::TelemetryPacket t = {};
    t.ms = now;
    t.roll_cd = (int16_t)(s.roll * 100); t.pitch_cd = (int16_t)(s.pitch * 100); t.yaw_cd = (int16_t)(s.yaw * 100);
    t.ur_cd = (int16_t)(s.out.ur * 100); t.up_cd = (int16_t)(s.out.up * 100); t.uy_cd = (int16_t)(s.out.uy * 100);
    t.vbat_mv = (uint16_t)(gVbat * 1000);
    t.mode = s.out.mode;
    t.state = s.out.state;
    t.flags = (s.imuOk ? proto::FLAG_IMU_OK : 0) | (lowBatt ? proto::FLAG_LOW_BATT : 0) |
              (s.out.armBlocked ? proto::FLAG_ARM_BLOCKED : 0) | (s.in.bench ? proto::FLAG_BENCH : 0) |
              (s.in.charging ? proto::FLAG_CHARGING : 0);
    t.flap_dhz = (uint8_t)constrain(s.out.flapHz * 10, 0, 255);
    t.link_pps = pps;
    t.alt_cm = (int16_t)constrain(s.alt * 100.0f, -32000.0f, 32000.0f);
    t.vz_cms = (int16_t)constrain(s.vz * 100.0f, -32000.0f, 32000.0f);
    const GpsFix& g = gps.fix();
    const bool fixOk = gpsFresh();
    t.lat_e7 = fixOk ? (int32_t)lround(g.lat * 1e7) : 0;
    t.lon_e7 = fixOk ? (int32_t)lround(g.lon * 1e7) : 0;
    t.gps_alt_dm = (int16_t)constrain(g.altMsl * 10.0f, -32000.0f, 32000.0f);
    t.gspeed_cms = (uint16_t)constrain(g.speed * 100.0f, 0.0f, 65000.0f);
    t.course_cd = (uint16_t)constrain(g.course * 100.0f, 0.0f, 35999.0f);
    t.sats = g.sats;
    t.hdop_d = (uint8_t)constrain(g.hdop * 10.0f, 0.0f, 255.0f);
    if (fixOk) t.flags |= proto::FLAG_GPS_FIX;
    if (s.out.rth) t.flags |= proto::FLAG_RTH;
    if (homeSet) t.flags |= proto::FLAG_HOME_SET;
    linkSendTelemetry(t);
  }

  // LED: charging = 1 s on / 1 s off, armed = on, failsafe / IMU fault = fast blink,
  //      low battery = double blink, idle = short blink every second
  bool led;
  if (s.in.charging) led = (now / 1000) % 2;
  else if (!s.imuOk || s.out.state == proto::ST_FAILSAFE) led = (now / 100) % 2;
  else if (lowBatt) led = (now % 1000) < 100 || ((now % 1000) > 200 && (now % 1000) < 300);
  else if (s.out.state == proto::ST_ARMED) led = true;
  else led = (now % 1000) < 100;
  digitalWrite(PIN_LED, led ? LOW : HIGH);

  delay(1);
}

// State shared between the control task, the radio callback and the USB CLI.
// Anything read/written from more than one task goes through gMux.
#pragma once
#include <Arduino.h>

#include "flight.h"
#include "protocol.h"

struct Snapshot {
  float roll = 0, pitch = 0, yaw = 0, rollAvg = 0, pitchAvg = 0;
  float gyro[3] = {0, 0, 0};   // filtered body rates, deg/s
  float acc[3] = {0, 0, 0};    // filtered body accel, g
  float alt = 0, vz = 0;       // m, m/s
  bool baroPresent = false, baroOk = false;
  FlightInputs in;
  FlightOutput out;
  bool imuPresent = false, imuOk = false;
  bool northValid = false;     // gyro heading aligned to GPS north (needed for RTH)
  uint32_t imuErrors = 0;
  uint32_t loopMaxUs = 0;
};

struct BenchState {
  bool active = false;
  float thr = 0, roll = 0, pitch = 0, yaw = 0;
  uint8_t mode = proto::MODE_MANUAL;
  uint32_t startMs = 0;
};

enum LogMode : uint8_t { LOG_OFF = 0, LOG_ATT, LOG_FFT, LOG_RAW };

struct LogRec {
  uint32_t t;
  uint8_t kind;   // 'A', 'F', 'R'
  uint8_t n;      // number of values used
  float v[12];
};

extern portMUX_TYPE gMux;

// radio -> control
extern FlightInputs gRadioIn;
extern uint32_t gLastRadioMs;
extern volatile uint32_t gRadioPktCount;
extern float gPendingTurn;
extern float gPendingAlt;     // metres, from CMD_ALT
extern volatile bool gCalibRequest;
extern volatile bool gSaveRequest;

// control -> everyone
extern Snapshot gSnap;
extern float gVbat;

// CLI -> control
extern BenchState gBench;
extern volatile bool gServoTest;
extern float gServoTestL, gServoTestR;
extern volatile uint8_t gLogMode;

// single-producer (control task) / single-consumer (loop) log queue
bool logPush(const LogRec& r);
bool logPop(LogRec& r);
extern volatile uint32_t gLogDropped;

// GPS navigation state published by loop() for the control task
struct GpsNav {
  bool ok = false, homeSet = false;
  float north = 0, east = 0;   // m from home
  float course = 0, speed = 0; // deg true, m/s
};
extern GpsNav gGpsNav;

// GPS (owned by loop(), see butterfly_fc.ino)
struct GpsFix;
const GpsFix& gpsFix();
bool gpsFresh();              // valid fix received within the last 2 s
uint32_t gpsBaud();           // 0 while searching for the module
uint32_t gpsSentences();

// link.cpp
bool linkBegin();
void linkSendTelemetry(proto::TelemetryPacket& t);

// cli.cpp
void cliPoll();
void cliDrainLog();

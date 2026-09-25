// Minimal NMEA 0183 parser for u-blox style GPS modules (GGA + RMC, any talker:
// GP / GN / GL / GA / BD / GB). Header-only, no Arduino dependency (unit-tested).
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct GpsFix {
  bool valid = false;        // GGA fix quality > 0
  uint8_t quality = 0;       // 0 none, 1 GPS, 2 DGPS/SBAS, 4/5 RTK, 6 estimated
  uint8_t sats = 0;
  float hdop = 99.9f;
  double lat = 0, lon = 0;   // degrees, WGS-84 (+N, +E)
  float altMsl = 0;          // m
  float speed = 0;           // ground speed, m/s
  float course = 0;          // course over ground, deg true
  uint32_t updatedMs = 0;    // time of the last valid position update
};

class NmeaParser {
 public:
  // Feed one received character. Returns true when a complete, checksum-valid
  // GGA or RMC sentence has been applied to the fix.
  bool feed(char c, uint32_t nowMs) {
    if (c == '$') { len_ = 0; active_ = true; return false; }
    if (!active_) return false;
    if (c == '\r' || c == '\n') {
      active_ = false;
      buf_[len_] = '\0';
      return process(nowMs);
    }
    if (len_ >= (int)sizeof(buf_) - 1) { active_ = false; return false; }  // overlong: drop
    buf_[len_++] = c;
    return false;
  }

  const GpsFix& fix() const { return fix_; }
  uint32_t sentences() const { return sentences_; }       // checksum-valid sentences of any type
  uint32_t checksumErrors() const { return crcErrors_; }

  // "ddmm.mmmm" / "dddmm.mmmm" + hemisphere -> signed degrees
  static double toDegrees(const char* v, char hemi) {
    if (!v || !*v) return 0;
    const double raw = strtod(v, nullptr);
    const double deg = floor(raw / 100.0);
    double d = deg + (raw - deg * 100.0) / 60.0;
    if (hemi == 'S' || hemi == 'W') d = -d;
    return d;
  }

 private:
  static int hexVal(char h) {
    if (h >= '0' && h <= '9') return h - '0';
    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
    return -1;
  }

  bool process(uint32_t nowMs) {
    char* star = strchr(buf_, '*');
    if (!star || star[1] == '\0' || star[2] == '\0') return false;
    const int hi = hexVal(star[1]), lo = hexVal(star[2]);
    uint8_t sum = 0;
    for (const char* p = buf_; p < star; ++p) sum ^= (uint8_t)*p;
    if (hi < 0 || lo < 0 || sum != (uint8_t)(hi * 16 + lo)) { ++crcErrors_; return false; }
    *star = '\0';
    ++sentences_;

    char* f[20];
    int n = 0;
    f[n++] = buf_;
    for (char* p = buf_; *p && n < 20; ++p)
      if (*p == ',') { *p = '\0'; f[n++] = p + 1; }
    if (strlen(f[0]) != 5) return false;
    const char* type = f[0] + 2;

    if (!strcmp(type, "GGA") && n >= 10) {
      fix_.quality = (uint8_t)atoi(f[6]);
      fix_.sats = (uint8_t)atoi(f[7]);
      fix_.hdop = *f[8] ? (float)atof(f[8]) : 99.9f;
      fix_.valid = fix_.quality > 0 && *f[2] && *f[4];
      if (fix_.valid) {
        fix_.lat = toDegrees(f[2], *f[3]);
        fix_.lon = toDegrees(f[4], *f[5]);
        fix_.altMsl = (float)atof(f[9]);
        fix_.updatedMs = nowMs;
      }
      return true;
    }
    if (!strcmp(type, "RMC") && n >= 9) {
      if (*f[2] == 'A' && *f[3] && *f[5]) {
        fix_.lat = toDegrees(f[3], *f[4]);
        fix_.lon = toDegrees(f[5], *f[6]);
        fix_.speed = (float)atof(f[7]) * 0.514444f;   // knots -> m/s
        if (*f[8]) fix_.course = (float)atof(f[8]);
        fix_.updatedMs = nowMs;
      }
      return true;
    }
    return false;
  }

  char buf_[100];
  int len_ = 0;
  bool active_ = false;
  GpsFix fix_;
  uint32_t sentences_ = 0, crcErrors_ = 0;
};

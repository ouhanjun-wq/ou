// Radio protocol shared by butterfly_fc and ground_station.
// KEEP THIS FILE IDENTICAL IN BOTH SKETCH FOLDERS.
//
// Transport: ESP-NOW broadcast on a fixed Wi-Fi channel. No MAC pairing needed;
// packets are filtered by MAGIC + netId and checked with CRC-16.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace proto {

constexpr uint8_t MAGIC        = 0xB7;
constexpr uint8_t WIFI_CHANNEL = 1;

enum PacketType : uint8_t {
  PKT_CONTROL   = 0x01,  // ground -> butterfly, 50 Hz stick state
  PKT_COMMAND   = 0x02,  // ground -> butterfly, one-shot command
  PKT_PARAM     = 0x03,  // ground -> butterfly, set a parameter by name
  PKT_TELEMETRY = 0x81,  // butterfly -> ground, 20 Hz
};

enum FlightMode : uint8_t {
  MODE_MANUAL       = 0,  // sticks drive wing offsets directly
  MODE_STABILIZE    = 1,  // sticks = target angle, self-levelling
  MODE_HEADING_HOLD = 2,  // stabilize + hold heading when yaw stick centred
};

enum Command : uint8_t {
  CMD_TURN       = 1,  // arg = degrees (+ right / - left), HEADING_HOLD only
  CMD_CALIB_GYRO = 2,  // disarmed only
  CMD_SAVE       = 3,  // save parameters to flash, disarmed only
};

enum State : uint8_t { ST_DISARMED = 0, ST_ARMED = 1, ST_FAILSAFE = 2 };

enum StatusFlag : uint8_t {
  FLAG_IMU_OK      = 1 << 0,
  FLAG_LOW_BATT    = 1 << 1,
  FLAG_ARM_BLOCKED = 1 << 2,  // arm switch on while throttle not low
  FLAG_BENCH       = 1 << 3,  // USB bench mode active
  FLAG_CHARGING    = 1 << 4,  // Type-C charger connected (arming locked)
};

struct __attribute__((packed)) Header {
  uint8_t magic;
  uint8_t netId;
  uint8_t type;
  uint8_t seq;
};

struct __attribute__((packed)) ControlPacket {
  Header  h;
  int16_t thr;    // 0 .. 1000
  int16_t roll;   // -500 .. 500 (right +)
  int16_t pitch;  // -500 .. 500 (nose up +)
  int16_t yaw;    // -500 .. 500 (nose right +)
  uint8_t mode;   // FlightMode
  uint8_t armed;  // 0 / 1
  uint16_t crc;
};

struct __attribute__((packed)) CommandPacket {
  Header  h;
  uint8_t cmd;
  int16_t arg;
  uint16_t crc;
};

struct __attribute__((packed)) ParamPacket {
  Header h;
  char   name[16];
  float  value;
  uint16_t crc;
};

struct __attribute__((packed)) TelemetryPacket {
  Header   h;
  uint32_t ms;
  int16_t  roll_cd, pitch_cd, yaw_cd;  // centi-degrees
  int16_t  ur_cd, up_cd, uy_cd;        // control outputs, centi-degrees
  uint16_t vbat_mv;
  uint8_t  mode, state, flags;
  uint8_t  flap_dhz;                   // flapping frequency, 0.1 Hz
  uint8_t  link_pps;                   // control packets received per second
  uint16_t crc;
};

// CRC-16/CCITT-FALSE
inline uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= (uint16_t)(*d++) << 8;
    for (int i = 0; i < 8; ++i) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}

template <class T>
inline void seal(T& p, uint8_t type, uint8_t netId, uint8_t seq) {
  p.h.magic = MAGIC;
  p.h.netId = netId;
  p.h.type = type;
  p.h.seq = seq;
  p.crc = crc16(reinterpret_cast<const uint8_t*>(&p), sizeof(T) - sizeof(uint16_t));
}

template <class T>
inline bool open(const uint8_t* data, int len, uint8_t type, uint8_t netId, T& out) {
  if (len != (int)sizeof(T)) return false;
  const Header* h = reinterpret_cast<const Header*>(data);
  if (h->magic != MAGIC || h->netId != netId || h->type != type) return false;
  uint16_t crc;
  __builtin_memcpy(&crc, data + sizeof(T) - sizeof(uint16_t), sizeof(crc));
  if (crc != crc16(data, sizeof(T) - sizeof(uint16_t))) return false;
  __builtin_memcpy(&out, data, sizeof(T));
  return true;
}

}  // namespace proto

// Radio protocol between the gesture glove and the hexapod (ESP-NOW broadcast, fixed channel).
// This file is identical in firmware/glove/ and firmware/hexapod/ (CI checks it).
//
// Axes (both the glove and the robot): x forward, y left, z up; turn > 0 = counter-clockwise
// (to the left) seen from above. Body pitch > 0 = nose down, roll > 0 = right side down.
#ifndef HX_PROTOCOL_H   // guard macro (not only #pragma once): the tests include both copies
#define HX_PROTOCOL_H
#include <stdint.h>
#include <string.h>

namespace hx {

constexpr uint8_t MAGIC = 0x6B;
constexpr uint8_t VERSION = 1;
constexpr uint8_t WIFI_CHANNEL = 1;
constexpr uint8_t DEFAULT_LINK_ID = 7;   // glove and robot must match (LINK command)

enum PacketType : uint8_t { PKT_GLOVE = 1, PKT_STATUS = 2 };

// What the robot should do with x / y / turn / pitch / roll.
enum Mode : uint8_t {
  MODE_STOP = 0,   // stand still (fist, unknown gesture, glove disarmed)
  MODE_WALK = 1,   // open hand: x = forward / back, turn = turn
  MODE_CRAB = 2,   // index finger: x = forward / back, y = sideways
  MODE_BODY = 3,   // victory: the body tilts like the hand (pitch / roll), feet stay put
};

// One-shot actions. The glove keeps sending the last event with its event_seq; the robot acts
// once each time event_seq changes (so a lost packet does not lose the event).
enum Event : uint8_t { EV_NONE = 0, EV_STAND_TOGGLE = 1, EV_NEXT_GAIT = 2 };

enum GloveFlags : uint8_t { GF_ARMED = 1 };
enum StatusFlags : uint8_t { SF_LOW_BATTERY = 1, SF_OBSTACLE = 2, SF_LIMIT = 4 };

// Robot states reported back to the glove.
enum RobotState : uint8_t { RS_RELAXED = 0, RS_STANDING_UP = 1, RS_READY = 2, RS_SITTING_DOWN = 3 };

struct __attribute__((packed)) Header {
  uint8_t magic, version, type, link_id, seq;
};

// Glove -> robot, 50 Hz.
struct __attribute__((packed)) GlovePacket {
  Header h;
  uint8_t mode;       // Mode
  uint8_t flags;      // GloveFlags
  uint8_t event;      // Event
  uint8_t event_seq;
  int8_t x, y, turn;  // -100 .. 100 (percent of the gait's top speed)
  int8_t pitch, roll; // -100 .. 100 (percent of the largest body tilt), MODE_BODY only
  uint8_t crc;
};

// Robot -> glove, 5 Hz.
struct __attribute__((packed)) StatusPacket {
  Header h;
  uint8_t state;      // RobotState
  uint8_t gait;       // 0 tripod, 1 ripple, 2 wave
  uint16_t vbat_mv;   // 0 = not measured
  uint8_t flags;      // StatusFlags
  uint8_t crc;
};

// CRC-8, polynomial 0x07.
inline uint8_t crc8(const uint8_t* d, int n) {
  uint8_t c = 0;
  for (int i = 0; i < n; ++i) {
    c ^= d[i];
    for (int b = 0; b < 8; ++b) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

template <class T>
inline void seal(T& p, PacketType type, uint8_t link_id, uint8_t seq) {
  p.h.magic = MAGIC;
  p.h.version = VERSION;
  p.h.type = type;
  p.h.link_id = link_id;
  p.h.seq = seq;
  p.crc = crc8(reinterpret_cast<const uint8_t*>(&p), (int)sizeof(T) - 1);
}

// Copies a received frame into `out` if it has the right size, type, link id and CRC.
template <class T>
inline bool open(const uint8_t* d, int len, PacketType type, uint8_t link_id, T& out) {
  if (len != (int)sizeof(T)) return false;
  memcpy(&out, d, sizeof(T));
  if (out.h.magic != MAGIC || out.h.version != VERSION || out.h.type != type) return false;
  if (out.h.link_id != link_id) return false;
  return out.crc == crc8(d, (int)sizeof(T) - 1);
}

}  // namespace hx

#endif  // HX_PROTOCOL_H

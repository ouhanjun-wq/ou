// Host-side unit tests for the hardware-independent bionic hand code.
//   g++ -std=gnu++11 -O1 -Wall -Wextra -Werror -I../bionic_hand_uno test_core.cpp -o test_core && ./test_core
#include <stdio.h>
#include <string.h>

#include "hand_core.h"

using namespace hc;

static int failures = 0;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (!(cond)) {                                \
      ++failures;                                 \
      printf("FAIL %s:%d  ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

static void testNormalize() {
  const Cal up = {200, 400, 700};
  CHECK(normalize(up, 200) == 0, "open -> 0: %d", normalize(up, 200));
  CHECK(normalize(up, 400) == 500, "half -> 500: %d", normalize(up, 400));
  CHECK(normalize(up, 700) == 1000, "closed -> 1000: %d", normalize(up, 700));
  CHECK(normalize(up, 300) == 250, "first segment midpoint: %d", normalize(up, 300));
  CHECK(normalize(up, 550) == 750, "second segment midpoint: %d", normalize(up, 550));
  CHECK(normalize(up, 0) == 0 && normalize(up, 1023) == 1000, "clamped outside the calibrated range");

  const Cal down = {800, 600, 300};   // pot turning the other way
  CHECK(normalize(down, 800) == 0 && normalize(down, 600) == 500 && normalize(down, 300) == 1000, "reversed pot");
  CHECK(normalize(down, 700) == 250, "reversed first segment: %d", normalize(down, 700));

  int16_t prev = -1;
  bool mono = true;
  for (int r = 0; r <= 1023; ++r) {
    const int16_t n = normalize(down, (int16_t)(1023 - r));
    if (n < prev) mono = false;
    prev = n;
  }
  CHECK(mono, "normalize is monotonic over the whole ADC range");

  CHECK(!calValid({300, 250, 600}), "non-monotonic calibration rejected");
  CHECK(!calValid({300, 320, 340}), "too small range rejected");
  CHECK(calValid({300, 330, 360}), "60 counts accepted");
  CHECK(normalize({300, 250, 600}, 500) == 0, "invalid calibration -> 0 (open, safe)");
}

static void testLerp() {
  CHECK(lerp(5, 0, 10, 0, 100) == 50, "lerp mid");
  CHECK(lerp(1, 0, 3, 0, 1000) == 333, "lerp rounding: %d", lerp(1, 0, 3, 0, 1000));
  CHECK(lerp(2, 0, 3, 0, 1000) == 667, "lerp rounding up: %d", lerp(2, 0, 3, 0, 1000));
  CHECK(lerp(2, 0, 3, 1000, 0) == 333, "lerp decreasing output: %d", lerp(2, 0, 3, 1000, 0));
  CHECK(lerp(1, 3, 0, 0, 1000) == 667, "lerp decreasing input: %d", lerp(1, 3, 0, 0, 1000));
}

static void testSmooth() {
  Smooth s;
  CHECK(s.update(400) == 400, "first sample passes straight through");
  // small noise inside the deadband does not move the output
  bool still = true;
  for (int i = 0; i < 200; ++i)
    if (s.update((int16_t)(400 + ((i & 1) ? 4 : -4))) != 400) still = false;
  CHECK(still, "+-4 noise is held by the deadband");
  // a real step gets through, and settles within 2 counts
  int16_t y = 0;
  int ticks = 0;
  for (; ticks < 100; ++ticks) {
    y = s.update(800);
    if (y >= 798) break;
  }
  CHECK(y >= 798 && y <= 800, "step settles at %d", y);
  CHECK(ticks <= 25, "step settles within 0.5 s at 50 Hz (%d ticks)", ticks);
  // the ends are reached exactly (a full fist really closes the hand)
  for (int i = 0; i < 100; ++i) y = s.update(1000);
  CHECK(y == 1000, "reaches 1000 exactly: %d", y);
  for (int i = 0; i < 100; ++i) y = s.update(0);
  CHECK(y == 0, "reaches 0 exactly: %d", y);
}

static void testLimitsAndSlew() {
  const Lim l = {1000, 2000};
  CHECK(toUs(l, 0) == 1000 && toUs(l, 1000) == 2000 && toUs(l, 500) == 1500, "limits map");
  const Lim inv = {2100, 900};
  CHECK(toUs(inv, 0) == 2100 && toUs(inv, 1000) == 900, "inverted servo");
  CHECK(toUs(l, -50) == 1000 && toUs(l, 5000) == 2000, "n clamped");
  CHECK(toUs({400, 2700}, 0) == US_MIN && toUs({400, 2700}, 1000) == US_MAX, "pulse clamped to 500..2500");
  CHECK(slew(1000, 1100, 40) == 1040 && slew(1000, 900, 40) == 960 && slew(1000, 1020, 40) == 1020, "slew");
}

static void testSettings() {
  Settings s = defaults();
  CHECK(settingsOk(s), "defaults are sealed");
  s.lim[2].us_open = 1234;
  CHECK(!settingsOk(s), "edited settings fail the CRC until sealed");
  seal(s);
  CHECK(settingsOk(s), "resealed");
  Settings blank;
  memset(&blank, 0xFF, sizeof(blank));   // erased EEPROM
  CHECK(!settingsOk(blank), "erased EEPROM rejected");
  const uint8_t v[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK(crc16(v, 9) == 0x29B1, "CRC-16/CCITT-FALSE check value: %04X", crc16(v, 9));
  CHECK(sizeof(Settings) < 1024, "fits the Uno EEPROM");
}

static void testGestures() {
  CHECK(findGesture("rock") >= 0 && findGesture("paper") == 0 && findGesture("nope") == -1, "gesture lookup");
  for (uint8_t g = 0; g < N_GESTURES; ++g)
    for (uint8_t i = 0; i < N; ++i)
      CHECK(GESTURES[g].n[i] >= 0 && GESTURES[g].n[i] <= N_MAX, "gesture %s value in range", GESTURES[g].name);
}

static Controller calibrated() {
  Controller c;
  for (uint8_t i = 0; i < N; ++i) {
    c.set.cal[i] = {200, 450, 700};
    c.set.lim[i] = {1000, 2000};
  }
  c.set.slew_us = 1000;   // no slew limit unless a test wants one
  c.begin();
  return c;
}

static void testMirror() {
  Controller c = calibrated();
  int16_t raw[N];
  for (uint8_t i = 0; i < N; ++i) raw[i] = 700;   // fist
  uint32_t t = 0;
  for (int k = 0; k < 50; ++k) c.update(raw, t += 20);
  bool closed = true;
  for (uint8_t i = 0; i < N; ++i) closed = closed && c.us[i] == 2000;
  CHECK(closed, "fist on the glove closes every servo (us[0] = %d)", c.us[0]);
  CHECK(!c.unplugged, "a fist is not mistaken for an unplugged glove");
}

static void testUnplug() {
  Controller c = calibrated();
  int16_t raw[N];
  for (uint8_t i = 0; i < N; ++i) raw[i] = 700;
  uint32_t t = 0;
  for (int k = 0; k < 50; ++k) c.update(raw, t += 20);
  // cable pulled out: every input floats up to ~1023 through the pull-ups
  for (uint8_t i = 0; i < N; ++i) raw[i] = 1023;
  for (int k = 0; k < 5; ++k) c.update(raw, t += 20);            // 100 ms: still debouncing
  CHECK(!c.unplugged && c.us[0] == 2000, "short glitch: pose unchanged");
  for (int k = 0; k < 10; ++k) c.update(raw, t += 20);           // 300 ms
  CHECK(c.unplugged, "unplugged after 200 ms");
  CHECK(c.us[0] == 2000, "holds the pose right after unplugging");
  for (int k = 0; k < 40; ++k) c.update(raw, t += 20);           // +800 ms
  CHECK(c.us[0] == 1000, "then opens the hand: us = %d", c.us[0]);
  // plugged back in: follows the glove again
  for (uint8_t i = 0; i < N; ++i) raw[i] = 450;
  for (int k = 0; k < 50; ++k) c.update(raw, t += 20);
  CHECK(!c.unplugged && c.us[0] == 1500, "reconnect: back to mirroring (us = %d)", c.us[0]);
}

static void testSlewInController() {
  Controller c = calibrated();
  c.set.slew_us = 40;
  int16_t raw[N];
  for (uint8_t i = 0; i < N; ++i) raw[i] = 700;
  c.update(raw, 20);
  CHECK(c.us[0] == 1040, "one tick moves at most slew_us: %d", c.us[0]);
  uint32_t t = 20;
  int ticks = 1;
  while (c.us[0] < 2000 && ticks < 100) c.update(raw, t += 20), ++ticks;
  CHECK(ticks == 25, "1000 us at 40 us/tick takes 25 ticks (0.5 s): %d", ticks);
}

static void testModes() {
  Controller c = calibrated();
  int16_t raw[N];
  for (uint8_t i = 0; i < N; ++i) raw[i] = 200;
  c.setMode(GESTURE);
  c.gesture = findGesture("scissors");
  c.update(raw, 20);
  CHECK(c.us[0] == 1000 && c.us[2] == 2000, "gesture overrides the glove");
  c.setMode(MANUAL);
  c.manual_us[3] = 1777;
  c.update(raw, 40);
  CHECK(c.us[3] == 1777 && c.us[2] == 2000, "manual: one servo moves, the others stay");
  c.setMode(DEMO);
  c.update(raw, 1000);
  const int8_t g0 = c.gesture;
  c.update(raw, 2600);
  CHECK(c.gesture != g0, "demo steps to the next gesture after 1.5 s");
}

int main() {
  testNormalize();
  testLerp();
  testSmooth();
  testLimitsAndSlew();
  testSettings();
  testGestures();
  testMirror();
  testUnplug();
  testSlewInController();
  testModes();
  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}

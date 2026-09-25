// Flash-string helpers and a tiny output interface.
// The Uno has 32 KB of flash and 2 KB of RAM: all text constants live in flash (PSTR), and
// replies are printed piece by piece instead of through printf (saves ~1.5 KB of flash).
// On a PC (unit tests) the same code uses normal strings.
#pragma once
#include <stdint.h>
#include <string.h>

#if defined(ARDUINO) && defined(__AVR__)
#include <avr/pgmspace.h>
#else
#ifndef PSTR
#define PSTR(s) (s)
#endif
#define strcmp_P strcmp
#endif

// Big functions are called from many places; on the AVR inlining them costs flash.
#if defined(__AVR__)
#define ARM_NOINLINE __attribute__((noinline))
#else
#define ARM_NOINLINE
#endif

namespace arm {

// Where replies go: Serial on the Uno, a string buffer in the unit tests.
class Out {
 public:
  virtual void text(const char* pgm) = 0;       // PSTR text
  virtual void num(long v) = 0;
  virtual void dec(float v, uint8_t digits) = 0;
  virtual void end() = 0;                       // end of line
};

// Parses "-12.5", "300", "+7". Returns false for anything else (smaller than strtod).
inline bool parseNum(const char* s, float& out) {
  bool neg = false, digits = false, frac = false;
  float v = 0, scale = 1;
  if (*s == '-' || *s == '+') neg = *s++ == '-';
  for (; *s; ++s) {
    if (*s >= '0' && *s <= '9') {
      digits = true;
      if (frac) {
        scale *= 0.1f;
        v += (*s - '0') * scale;
      } else {
        v = v * 10 + (*s - '0');
      }
    } else if (*s == '.' && !frac) {
      frac = true;
    } else {
      return false;
    }
  }
  out = neg ? -v : v;
  return digits;
}

}  // namespace arm

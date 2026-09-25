// Two wing servos on LEDC hardware PWM.
#pragma once
#include <Arduino.h>

#include "params.h"

class ServoOut {
 public:
  bool begin(int pinL, int pinR);
  // Wing angles in degrees (+ = up) -> pulse widths using dir / trim / us-per-deg params.
  void writeWings(float wingL, float wingR, const Params& p);
  void writeUs(int usL, int usR);

 private:
  int pinL_ = -1, pinR_ = -1;
};

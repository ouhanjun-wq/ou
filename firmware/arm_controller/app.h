// Globals shared between the sketch (.ino) and cli.cpp.
#pragma once
#include <Arduino.h>

#include "motion.h"
#include "pca9685.h"
#include "params.h"

extern Params P;
extern arm::ArmCore core;
extern arm::Sensors sensors;
extern PCA9685 pwm;
extern float rawUs[NJ];        // > 0: calibration override, this pulse goes to the servo as-is

// Configuration commands (lower or upper case). Returns false if the word is unknown.
bool cliCommand(char* line, char* reply, size_t n);
float jointPulse(int j);       // pulse currently sent to joint j (us)

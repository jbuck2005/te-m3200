// Minimal stand-in for <Arduino.h> so the library builds on a PC for tests/run.sh. Not used on real boards.
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
inline void delay(unsigned long) {}

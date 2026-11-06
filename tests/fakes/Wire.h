// Fake TwoWire for tests/run.sh: replays one 4-byte packet and can simulate a NACK or a short read.
#pragma once
#include <Arduino.h>

struct TwoWire {
  uint8_t packet[4] = {0, 0, 0, 0};                                             // what the "sensor" returns
  uint8_t bytes = 4;                                                            // bytes returned by requestFrom (< 4 = short read)
  uint8_t ack = 0;                                                              // endTransmission() result (0 = ACK, 2 = NACK on address)
  uint8_t pos = 0;
  void beginTransmission(uint8_t) {}
  uint8_t endTransmission() { return ack; }
  uint8_t requestFrom(uint8_t, uint8_t) { pos = 0; return bytes; }
  int available() { return bytes - pos; }
  int read() { return packet[pos++]; }
  void set(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) { packet[0] = b0; packet[1] = b1; packet[2] = b2; packet[3] = b3; }
};
extern TwoWire Wire;

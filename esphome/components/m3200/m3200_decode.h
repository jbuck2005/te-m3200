/*    m3200_decode.h - packet decoding and unit conversion for the TE Connectivity M3200, shared by both drivers
 *
 *    Pure functions only: no Arduino, ESPHome or I2C code, so the same file builds in the Arduino library,
 *    the ESPHome component and the host-side tests (tests/).
 *
 *    THE SAME FILE LIVES IN TWO PLACES - keep them identical (tests/run.sh checks this):
 *      arduino/M3200/src/m3200_decode.h            (an Arduino library must be self-contained)
 *      esphome/components/m3200/m3200_decode.h     (ESPHome only downloads the component folder)
 *
 *    Packet layout (Read_DF4, I2C guide p4):
 *      b[0] = S1 S0 B13..B8     status bits [15:14] + upper 6 bits of the 14-bit pressure count
 *      b[1] = B7..B0            lower 8 bits of the pressure count
 *      b[2] = T10..T3           upper 8 bits of the 11-bit temperature count
 *      b[3] = T2 T1 T0 x x x x x  lower 3 bits of the temperature count + 5 junk bits
 */
#pragma once                                                                    // only include this file once per compile

#include <math.h>                                                               // NAN
#include <stdint.h>                                                             // uint8_t, uint16_t

namespace m3200_decode {

static const uint8_t STATUS_OK = 0;                                             // 0b00 normal operation, fresh data
static const uint8_t STATUS_RESERVED = 1;                                       // 0b01 reserved by TE
static const uint8_t STATUS_STALE = 2;                                          // 0b10 already fetched since the last measurement
static const uint8_t STATUS_FAULT = 3;                                          // 0b11 fault - latched until power-on reset

static const uint16_t MAX_COUNTS = 16383;                                       // largest 14-bit count

struct Packet {                                                                 // one decoded 4-byte packet
  uint8_t status;                                                               // STATUS_OK / _RESERVED / _STALE / _FAULT
  uint16_t raw_pressure;                                                        // 14-bit pressure count, 0..16383
  uint16_t raw_temperature;                                                     // 11-bit temperature count, 0..2047
};

inline Packet decode(const uint8_t b[4]) {                                      // split the 4 bytes into status + counts
  Packet p;
  p.status = b[0] >> 6;                                                         // top 2 bits
  p.raw_pressure = (uint16_t) (((uint16_t) (b[0] & 0x3F) << 8) | b[1]);         // drop the status bits, join the 14 bits
  p.raw_temperature = (uint16_t) (((uint16_t) b[2] << 3) | (b[3] >> 5));        // 8 high bits + top 3 bits of b[3] (junk masked off)
  return p;
}

inline bool carries_data(uint8_t status) {                                      // only ok and stale packets hold valid counts
  return status == STATUS_OK || status == STATUS_STALE;
}

inline bool valid_pressure_range(uint16_t zero, uint16_t span, float pmin, float pmax) {
  return zero < span && span <= MAX_COUNTS && pmax > pmin;                      // pmax > pmin is false for NAN too
}

inline bool in_window(uint16_t counts, uint16_t lo, uint16_t hi) {              // plausibility check on a raw pressure count
  return counts >= lo && counts <= hi;
}

/*
 *  Pressure (datasheet p6):  counts = (span - zero) / (Pmax - Pmin) x (P - Pmin) + zero
 *  Rearranged:               P = (counts - zero) x (Pmax - Pmin) / (span - zero) + Pmin
 *  With the defaults (1000 / 15000) a 100 psi part reading 8000 counts gives 50.0 psi. NAN for an invalid range.
 */
inline float counts_to_pressure(uint16_t counts, uint16_t zero, uint16_t span, float pmin, float pmax) {
  if (!valid_pressure_range(zero, span, pmin, pmax)) return NAN;                // don't invent a number
  float fraction = ((float) counts - (float) zero) / (float) (span - zero);     // 0.0 at Pmin, 1.0 at Pmax
  return fraction * (pmax - pmin) + pmin;
}

/*
 *  Temperature (datasheet p7): 11 bits span -50 .. +150 C, so T = counts x 200 / 2048 - 50
 *  e.g. 512 -> 0.0 C, 767 -> 24.9 C, 1075 -> 55.0 C (datasheet table)
 */
inline float counts_to_temperature(uint16_t counts) {
  return (float) counts * 200.0f / 2048.0f - 50.0f;
}

}  // namespace m3200_decode

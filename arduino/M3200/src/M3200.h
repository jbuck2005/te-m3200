/*    M3200.h - Arduino driver for the TE Connectivity M3200 digital (I2C) pressure transducer
 *
 *    Initial coding: 20200812        jbuck2005
 *    Last modified : 20261103        jbuck2005
 *
 *    Nov 01 2026
 *      Turned the original standalone sketch into a reusable library (this file + M3200.cpp)
 *      Temperature now uses the datasheet formula (p7) - the sketch's slope/intercept version was off
 *      Added sleep-mode support (measurement request before each read)
 *
 *    Nov 03 2026
 *      Sleep mode is now on by default - it works with both part types, while off froze sleep-mode parts
 *      Added staleCount() so a sketch can spot frozen readings
 *
 *    Part-number     : M32JM-000105-100PG (0..100 psi gauge, sleep mode, address 0x28)
 *    I2C Reference   : docs/Interfacing_to_DigitalPressure_Modules_A3.pdf
 *    Sensor datasheet: docs/ENG_DS_M3200_A21.pdf (rev A21, 06/2026; same page numbers as the older A20 copy in docs/)
 *
 *    Wiring (cable version, datasheet p4):
 *      Red   = VCC   (2.7 .. 5.0 V)
 *      Black = GND
 *      Green = SDA   (use 4k7 pull-up)
 *      White = SCL   (use 4k7 pull-up)
 *
 *    {S} = start / stop    # = bit / address    <R> = read/write bit    [A] = ack    [N] = no ack
 *
 *    (1) READ_MR  = {S} 6 5 4 3 2 1 0 <R> [A] {S}                        wake up + take one measurement (sleep-mode parts)
 *        WRITE_MR = {S} 6 5 4 3 2 1 0 <W> [A] {S}                        what this driver actually sends - same effect (see requestMeasurement())
 *    (4) READ_DF4 = {S} 6 5 4 3 2 1 0 <R> [A] | 15 14 13 12 11 10 9 8 [A] | 7 6 5 4 3 2 1 0 [A] | 10 9 8 7 6 5 4 3 [A] | 2 1 0 x x x x x [N] {S}
 *                     ----slave----             S1 S0 -bridge data-         --bridge data--       ---temperature--       -temp- -junk-
 *
 *    byte 0 : S1 S0 B13..B8       status bits [15:14] + upper 6 bits of the 14-bit pressure (bridge) count
 *    byte 1 : B7..B0              lower 8 bits of the pressure count
 *    byte 2 : T10..T3             upper 8 bits of the 11-bit temperature count
 *    byte 3 : T2 T1 T0 x x x x x  lower 3 bits of the temperature count + 5 junk bits (ignore them)
 *
 *    Status bits [15:14]:  0b00 = normal   0b01 = reserved   0b10 = stale data   0b11 = fault (latched until power cycle)
 *
 *    Transfer functions (datasheet p6 / p7):
 *      pressure    = (counts - 1000) x (Pmax - Pmin) / 14000 + Pmin
 *      temperature = counts x 200 / 2048 - 50                              (degrees C)
 */
#pragma once                                                                    // only include this file once per compile

#include <Arduino.h>                                                            // core Arduino types (uint8_t, NAN, delay ...)
#include <Wire.h>                                                               // Arduino I2C library (TwoWire)

/*********************************************************************************************************************
 * DEFAULTS
 ********************************************************************************************************************/
#define M3200_DEFAULT_ADDRESS 0x28                                              // address for ordering-code address digit 0 (1=0x36 2=0x46 3=0x48 4=0x51)
#define M3200_ZERO_COUNTS     1000                                              // typical count at zero pressure, same in A20 and A21 (p3 tolerance: A20 750..1250, A21 790..1210)
#define M3200_SPAN_COUNTS     15000                                             // typical count at full scale, same in A20 and A21 (p3 tolerance: A20 14720..15250, A21 14790..15210)
#define M3200_MR_DELAY_MS     15                                                // default wait after a measurement request; 8.4 ms max at nominal clock (datasheet p3) + margin
#define M3200_MIN_VALID_COUNTS 250                                               // default plausibility window: a count outside it is almost always a
#define M3200_MAX_VALID_COUNTS 15750                                             //   wiring, supply or over-range problem (about -5 %..+105 % of full scale)
#define M3200_MAX_COUNTS      16383                                             // largest 14-bit count
#define M3200_STALE_WARNING_COUNT 5                                             // this many stale packets in a row = values are frozen (wrong sleep mode?)

/*********************************************************************************************************************
 * STATUS CODES
 ********************************************************************************************************************/
enum M3200Status : uint8_t {                                                    // result of a read - the first four mirror the sensor's own status bits [15:14]
  M3200_OK        = 0,                                                          // 0b00 normal operation, good (fresh) data packet
  M3200_RESERVED  = 1,                                                          // 0b01 reserved by TE - should never happen, treated as an error
  M3200_STALE     = 2,                                                          // 0b10 stale: already fetched since the last measurement (still valid, just not new)
  M3200_FAULT     = 3,                                                          // 0b11 fault in the sensor - stays latched until the sensor is power cycled
  M3200_OUT_OF_RANGE = 0xFE,                                                    // not from the sensor: pressure count outside the plausibility window (setValidCountRange)
  M3200_I2C_ERROR = 0xFF                                                        // not from the sensor: no answer on the bus, or fewer than 4 bytes came back
};                                                                              // end of M3200Status

/*********************************************************************************************************************
 * M3200 CLASS
 ********************************************************************************************************************/
class M3200 {                                                                   // one M3200 object per physical sensor
 public:                                                                        // functions your sketch can call
  // pressureMax / pressureMin = rated range of your part, in whatever unit you want results in (psi, bar, kPa ...)
  //   M32JM-000105-100PG (gauge)     ->  M3200 sensor(100);           0..100 psi
  //   ...-200PC (compound)           ->  M3200 sensor(200, -14.7);    -14.7..200 psi
  explicit M3200(float pressureMax, float pressureMin = 0.0f);                  // constructor - stores the range, does not touch the bus yet

  bool begin(TwoWire &wire = Wire, uint8_t address = M3200_DEFAULT_ADDRESS);    // call Wire.begin() first; true = sensor answered with usable data
                                                                                //   false: lastStatus() says why (i2c error = no answer, fault = needs power cycle)
  bool requestMeasurement();                                                    // sleep-mode parts only: wake the sensor + start one conversion (non-blocking)
  M3200Status fetch();                                                          // read the 4-byte packet waiting in the sensor (never blocks)
  M3200Status read();                                                           // all-in-one: requestMeasurement() + wait 15 ms (sleep mode only) + fetch()

  float pressure() const { return pressure_; }                                  // pressure from the last read, in your unit; NAN until the first good read
  float temperature() const { return temperature_; }                            //   and after a fault, reserved status or bus error, so old values never pass as new
  bool isValid() const { return lastStatus_ == M3200_OK || lastStatus_ == M3200_STALE; } // true = pressure() / temperature() hold usable data
                                                                                // (out of range: raw counts kept, values NAN)
  uint16_t rawPressure() const { return rawPressure_; }                         // raw 14-bit counts (0..16383) of the last ok/stale packet - kept after errors, for diagnosis
  uint16_t rawTemperature() const { return rawTemperature_; }                   // raw 11-bit counts (0..2047) of the last ok/stale packet - kept after errors
  M3200Status lastStatus() const { return lastStatus_; }                        // status returned by the most recent fetch()
  uint8_t staleCount() const { return staleCount_; }                            // stale packets in a row; >= M3200_STALE_WARNING_COUNT = frozen values

  void setSleepMode(bool enabled) { sleepMode_ = enabled; }                     // on by default; false saves the wait per read() on non-sleep parts (sleep digit = 0)
  void setMeasurementDelay(uint8_t ms) { measurementDelayMs_ = ms ? ms : 1; }   // wait used by read() after a request (default 15 ms, minimum 1)
  bool setPressureRange(float pressureMax, float pressureMin = 0.0f);           // change the range; false (and unchanged) unless max > min
  bool setValidCountRange(uint16_t minCounts, uint16_t maxCounts);              // plausibility window (default 250..15750); 0, 16383 = accept everything
  bool setCountRange(uint16_t zeroCounts, uint16_t spanCounts);                 // trim a unit (defaults 1000 / 15000); false (and unchanged) unless zero < span <= 16383

  float countsToPressure(uint16_t counts) const;                                // raw counts -> pressure (no hardware needed); NAN if the range is invalid
  static float countsToTemperature(uint16_t counts);                            // raw counts -> degrees C (no hardware needed)
  static const char *statusToString(M3200Status status);                        // "ok", "stale", "fault" ... for printing

 private:                                                                       // internal state - use the functions above instead
  TwoWire *wire_ = nullptr;                                                     // I2C bus the sensor is on (set by begin())
  uint8_t address_ = M3200_DEFAULT_ADDRESS;                                     // sensor's 7-bit I2C address
  bool sleepMode_ = true;                                                       // true = send a measurement request before every read() (safe for both part types)
  uint8_t measurementDelayMs_ = M3200_MR_DELAY_MS;                              // wait between the request and the fetch in read()

  float pressureMin_;                                                           // pressure at zeroCounts_ (bottom of the range)
  float pressureMax_;                                                           // pressure at spanCounts_ (top of the range)
  uint16_t zeroCounts_ = M3200_ZERO_COUNTS;                                     // count the sensor outputs at pressureMin_
  uint16_t minValidCounts_ = M3200_MIN_VALID_COUNTS;                            // pressure counts below this are reported as M3200_OUT_OF_RANGE
  uint16_t maxValidCounts_ = M3200_MAX_VALID_COUNTS;                            // ...and above this
  uint16_t spanCounts_ = M3200_SPAN_COUNTS;                                     // count the sensor outputs at pressureMax_

  uint16_t rawPressure_ = 0;                                                    // last good 14-bit pressure count
  uint16_t rawTemperature_ = 0;                                                 // last good 11-bit temperature count
  float pressure_ = NAN;                                                        // pressure from the last read (NAN = no usable reading)
  float temperature_ = NAN;                                                     // temperature from the last read (NAN = no usable reading)
  M3200Status lastStatus_ = M3200_I2C_ERROR;                                    // nothing has been read yet

  M3200Status fail_(M3200Status status);                                        // record a failed read: values -> NAN, return status
  uint8_t staleCount_ = 0;                                                      // stale packets in a row (reset by a fresh one)
};                                                                              // end of class M3200

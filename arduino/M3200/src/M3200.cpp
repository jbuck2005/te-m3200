/*    M3200.cpp - Arduino driver for the TE Connectivity M3200 digital (I2C) pressure transducer
 *
 *    See M3200.h for the packet layout, wiring and change history.
 *    Page numbers refer to docs/ENG_DS_M3200_A21.pdf (datasheet, rev A21; pages match A20) and docs/Interfacing_to_DigitalPressure_Modules_A3.pdf (I2C guide).
 */
#include "M3200.h"                                                              // class definition, constants and status codes
#include "m3200_decode.h"                                                       // packet decoding + conversions shared with the ESPHome component

/*********************************************************************************************************************
 * CONSTRUCTOR
 ********************************************************************************************************************/
M3200::M3200(float pressureMax, float pressureMin)                              // runs when your sketch says: M3200 sensor(100);
    : pressureMin_(pressureMin), pressureMax_(pressureMax) {}                   // just remember the range - the bus is not touched until begin()

/*********************************************************************************************************************
 * BEGIN
 ********************************************************************************************************************/
bool M3200::begin(TwoWire &wire, uint8_t address) {                             // connect this object to an I2C bus + address
  wire_ = &wire;                                                                // remember which bus (Wire, Wire1 ...) the sensor is on
  address_ = address;                                                           // remember the sensor's address (0x28 unless ordered otherwise)
  M3200Status status = fetch();                                                 // dummy read: proves the sensor answers + clears the stale first packet after power-up
  return status == M3200_OK || status == M3200_STALE;                           // a faulted (or reserved) sensor answers but can't be used - report that too
}                                                                               // end of begin()

/*********************************************************************************************************************
 * REQUESTMEASUREMENT
 ********************************************************************************************************************/
bool M3200::requestMeasurement() {                                              // measurement request, sent as a Write_MR (see below)
  if (wire_ == nullptr) return false;                                           // begin() was never called - nothing to talk to
  /*
   * The I2C guide (p4, figure 1.6 example 1) shows the request as a Read_MR: the address with the READ bit and no data
   *   {S} 6 5 4 3 2 1 0 <R> [A] {S}
   * Wire can't send a zero-byte read portably, so this sends the address with the WRITE bit instead (a "Write_MR"):
   *   {S} 6 5 4 3 2 1 0 <W> [A] {S}
   * The sensor's conditioning chip treats both as a measurement request.
   * A sleep-mode sensor wakes up, takes one measurement and has it ready ~8.4 ms later (datasheet p3)
   * Clocking out the address (rather than an empty start/stop) also avoids the quirk in section 1.8 of the I2C guide
   */
  wire_->beginTransmission(address_);                                           // queue a transmission to the sensor's address
  return wire_->endTransmission() == 0;                                         // send it: 0 = sensor ACKed, anything else = no answer
}                                                                               // end of requestMeasurement()

/*********************************************************************************************************************
 * FETCH
 ********************************************************************************************************************/
M3200Status M3200::fetch() {                                                    // Read_DF4 (I2C guide p4, figure 1.6 example 4)
  if (wire_ == nullptr) return fail_(M3200_I2C_ERROR);                          // begin() was never called

  if (wire_->requestFrom(address_, (uint8_t) 4) != 4) {                         // ask for 4 bytes: Wire ACKs bytes 0-2, NACKs byte 3, then stops
    while (wire_->available()) wire_->read();                                   // short read - throw away whatever did arrive
    return fail_(M3200_I2C_ERROR);                                              // and report a bus error
  }                                                                             // end of short-read check
  uint8_t buf[4];                                                               // buf[0] = status + pressure high, buf[1] = pressure low, buf[2] = temp high, buf[3] = temp low
  for (uint8_t i = 0; i < 4; i++) buf[i] = wire_->read();                       // copy the 4 received bytes out of the Wire buffer

  /*
   * buf[0] = SSHHHHHH    S = status bits [15:14]     H = pressure bits [13:8]
   * buf[1] = LLLLLLLL    L = pressure bits [7:0]
   * buf[2] = TTTTTTTT    T = temperature bits [10:3]
   * buf[3] = tttxxxxx    t = temperature bits [2:0]  x = junk
   */
  m3200_decode::Packet packet = m3200_decode::decode(buf);                      // split into status + 14-bit pressure + 11-bit temperature
  M3200Status status = (M3200Status) packet.status;                             // the 4 sensor status values match M3200Status 0..3
  if (m3200_decode::carries_data(packet.status)) {                              // only OK and STALE packets carry valid data
    rawPressure_ = packet.raw_pressure;                                         // 0..16383
    rawTemperature_ = packet.raw_temperature;                                   // 0..2047
    pressure_ = countsToPressure(rawPressure_);                                 // counts -> pressure in your unit
    temperature_ = countsToTemperature(rawTemperature_);                        // counts -> degrees C
    if (!m3200_decode::in_window(rawPressure_, minValidCounts_, maxValidCounts_)) // implausible count: wiring, supply or over-range, not a real reading
      return fail_(M3200_OUT_OF_RANGE);                                         //   rawPressure() still shows the count for diagnosis
  } else {                                                                      // RESERVED / FAULT: no usable data in this packet
    return fail_(status);                                                       // so don't leave the previous values looking current
  }                                                                             // end of good-data check
  if (status == M3200_OK) staleCount_ = 0;                                      // the sensor is measuring - restart the stale count
  else if (status == M3200_STALE && staleCount_ < 255) staleCount_++;           // one more stale packet in a row (stop at 255 so it can't wrap to 0)
  return lastStatus_ = status;                                                  // remember and return the status
}                                                                               // end of fetch()

/*********************************************************************************************************************
 * FAIL_
 ********************************************************************************************************************/
M3200Status M3200::fail_(M3200Status status) {                                  // a read that produced no usable data
  pressure_ = NAN;                                                              // NAN, not the previous value: a sketch that forgets to check
  temperature_ = NAN;                                                           //   the status prints "nan" instead of a stale number
  return lastStatus_ = status;                                                  // remember and return the status
}                                                                               // end of fail_()

/*********************************************************************************************************************
 * READ
 ********************************************************************************************************************/
M3200Status M3200::read() {                                                     // the easy way: one call per reading
  if (sleepMode_) {                                                             // sleep-mode parts need waking up first
    if (!requestMeasurement()) return fail_(M3200_I2C_ERROR);                   // sensor did not ACK the measurement request
    delay(measurementDelayMs_);                                                 // give it time to finish (15 ms default, 8.4 ms max per datasheet p3) - this blocks
  }                                                                             // end of sleep-mode request
  return fetch();                                                               // read the result
}                                                                               // end of read()

/*********************************************************************************************************************
 * SETTINGS
 ********************************************************************************************************************/
bool M3200::setPressureRange(float pressureMax, float pressureMin) {            // same meaning as the constructor arguments
  if (!(pressureMax > pressureMin)) return false;                               // also catches NAN; max == min would make every reading equal min
  pressureMax_ = pressureMax;                                                   // pressure at the full-scale count
  pressureMin_ = pressureMin;                                                   // pressure at the zero count
  return true;                                                                  // range accepted
}                                                                               // end of setPressureRange()

bool M3200::setValidCountRange(uint16_t minCounts, uint16_t maxCounts) {       // e.g. setValidCountRange(0, 16383) to turn the check off
  if (minCounts > maxCounts || maxCounts > M3200_MAX_COUNTS) return false;      // empty or impossible window
  minValidCounts_ = minCounts;                                                  // lowest count treated as a real reading
  maxValidCounts_ = maxCounts;                                                  // highest count treated as a real reading
  return true;                                                                  // window accepted
}                                                                               // end of setValidCountRange()

bool M3200::setCountRange(uint16_t zeroCounts, uint16_t spanCounts) {           // e.g. measured 1012 at 0 psi -> setCountRange(1012, 15000)
  if (zeroCounts >= spanCounts || spanCounts > M3200_MAX_COUNTS) return false;  // span <= zero divides by zero or flips the scale
  zeroCounts_ = zeroCounts;                                                     // count at pressureMin_
  spanCounts_ = spanCounts;                                                     // count at pressureMax_
  return true;                                                                  // range accepted
}                                                                               // end of setCountRange()

/*********************************************************************************************************************
 * CONVERSIONS
 ********************************************************************************************************************/
float M3200::countsToPressure(uint16_t counts) const {                          // datasheet p6 - formula in m3200_decode.h
  return m3200_decode::counts_to_pressure(counts, zeroCounts_, spanCounts_, pressureMin_, pressureMax_);  // NAN if the range is invalid
}                                                                               // end of countsToPressure()

float M3200::countsToTemperature(uint16_t counts) {                             // datasheet p7 - formula in m3200_decode.h
  return m3200_decode::counts_to_temperature(counts);                           // 0..2047 counts -> -50 .. +150 C
}                                                                               // end of countsToTemperature()

/*********************************************************************************************************************
 * STATUSTOSTRING
 ********************************************************************************************************************/
const char *M3200::statusToString(M3200Status status) {                         // handy for Serial.print()
  switch (status) {                                                             // pick the text for this status
    case M3200_OK:        return "ok";                                          // 0b00
    case M3200_RESERVED:  return "reserved";                                    // 0b01
    case M3200_STALE:     return "stale";                                       // 0b10
    case M3200_FAULT:     return "fault";                                       // 0b11
    case M3200_OUT_OF_RANGE: return "out of range";                             // count outside setValidCountRange()
    case M3200_I2C_ERROR: return "i2c error";                                   // no answer / short read
  }                                                                             // end of switch
  return "unknown";                                                             // should never get here
}                                                                               // end of statusToString()

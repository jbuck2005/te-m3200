// Tests for the Arduino M3200 class, run on a PC against the fake TwoWire in tests/fakes.
#include <string.h>

#include "M3200.h"
#include "check.h"

TwoWire Wire;

int main() {
  // good packet: begin(), read(), values
  Wire.set(0x1F, 0x40, 0x55, 0x60);
  M3200 s(100);
  CHECK(s.begin());
  CHECK(s.read() == M3200_OK);
  CHECK(s.isValid());
  CHECK_NEAR(s.pressure(), 50.0f, 1e-3f);
  CHECK_NEAR(s.temperature(), 16.70f, 0.01f);
  CHECK(s.rawPressure() == 8000 && s.rawTemperature() == 683);

  // stale packets are valid, and counted
  Wire.set(0x9F, 0x40, 0x55, 0x60);
  for (int i = 0; i < M3200_STALE_WARNING_COUNT; i++) CHECK(s.read() == M3200_STALE);
  CHECK(s.staleCount() == M3200_STALE_WARNING_COUNT);
  CHECK(s.isValid() && !isnan(s.pressure()));
  Wire.set(0x1F, 0x40, 0x55, 0x60);
  CHECK(s.read() == M3200_OK && s.staleCount() == 0);

  // fault and reserved: NAN values, raw counts kept for diagnosis
  Wire.set(0xDF, 0x40, 0x55, 0x60);
  CHECK(s.read() == M3200_FAULT);
  CHECK(isnan(s.pressure()) && isnan(s.temperature()) && !s.isValid());
  CHECK(s.rawPressure() == 8000);
  Wire.set(0x5F, 0x40, 0x55, 0x60);
  CHECK(s.read() == M3200_RESERVED && isnan(s.pressure()));

  // bus errors: short read, NACK on the measurement request
  Wire.set(0x1F, 0x40, 0x55, 0x60);
  Wire.bytes = 2;
  CHECK(s.read() == M3200_I2C_ERROR && isnan(s.pressure()));
  Wire.bytes = 4;
  Wire.ack = 2;
  CHECK(s.read() == M3200_I2C_ERROR && isnan(s.pressure()));
  Wire.ack = 0;
  CHECK(s.read() == M3200_OK && !isnan(s.pressure()));                         // recovers

  // plausibility window
  Wire.set(0x00, 0x00, 0x55, 0x60);
  CHECK(s.read() == M3200_OUT_OF_RANGE && isnan(s.pressure()) && s.rawPressure() == 0);
  Wire.set(0x3F, 0xFF, 0x55, 0x60);
  CHECK(s.read() == M3200_OUT_OF_RANGE && s.rawPressure() == 16383);
  CHECK(s.setValidCountRange(0, 16383));
  CHECK(s.read() == M3200_OK);
  CHECK(!s.setValidCountRange(10, 5) && !s.setValidCountRange(0, 16384));

  // range setters reject bad input and keep the old range
  CHECK(!s.setCountRange(1000, 1000) && !s.setCountRange(15000, 1000) && !s.setCountRange(1000, 16384));
  CHECK(!s.setPressureRange(5, 5) && !s.setPressureRange(NAN, 0));
  CHECK(s.setCountRange(1000, 15000) && s.setPressureRange(100));
  CHECK(s.setValidCountRange(250, 15750));
  Wire.set(0x1F, 0x40, 0x55, 0x60);
  CHECK(s.read() == M3200_OK);
  CHECK_NEAR(s.pressure(), 50.0f, 1e-3f);
  M3200 bad(0, 0);                                                              // invalid range from the constructor
  CHECK(isnan(bad.countsToPressure(8000)));

  // begin() on a faulted or missing sensor
  Wire.set(0xDF, 0x40, 0x55, 0x60);
  M3200 f(100);
  CHECK(!f.begin() && f.lastStatus() == M3200_FAULT);
  Wire.bytes = 0;
  M3200 m(100);
  CHECK(!m.begin() && m.lastStatus() == M3200_I2C_ERROR);
  Wire.bytes = 4;

  // read() without begin()
  M3200 n(100);
  CHECK(n.read() == M3200_I2C_ERROR);

  CHECK(strcmp(M3200::statusToString(M3200_OUT_OF_RANGE), "out of range") == 0);
  CHECK(strcmp(M3200::statusToString(M3200_FAULT), "fault") == 0);

  return tests_done("test_arduino");
}

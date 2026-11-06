// Tests for m3200_decode.h against the datasheet's own tables (ENG_DS_M3200 p6/p7) and the README worked example.
#include "../arduino/M3200/src/m3200_decode.h"
#include "check.h"

using namespace m3200_decode;

int main() {
  // README worked example: 1F 40 55 60 -> normal, 8000 counts, 683 counts
  const uint8_t ex[4] = {0x1F, 0x40, 0x55, 0x60};
  Packet p = decode(ex);
  CHECK(p.status == STATUS_OK);
  CHECK(p.raw_pressure == 8000);
  CHECK(p.raw_temperature == 683);
  CHECK_NEAR(counts_to_pressure(p.raw_pressure, 1000, 15000, 0, 100), 50.0f, 1e-4f);
  CHECK_NEAR(counts_to_temperature(p.raw_temperature), 16.70f, 0.01f);

  // status bits
  const uint8_t st[4][4] = {{0x1F, 0x40, 0, 0}, {0x5F, 0x40, 0, 0}, {0x9F, 0x40, 0, 0}, {0xDF, 0x40, 0, 0}};
  CHECK(decode(st[0]).status == STATUS_OK);
  CHECK(decode(st[1]).status == STATUS_RESERVED);
  CHECK(decode(st[2]).status == STATUS_STALE);
  CHECK(decode(st[3]).status == STATUS_FAULT);
  CHECK(decode(st[2]).raw_pressure == 8000);                                   // status bits never leak into the count
  CHECK(carries_data(STATUS_OK) && carries_data(STATUS_STALE));
  CHECK(!carries_data(STATUS_RESERVED) && !carries_data(STATUS_FAULT));

  // junk bits in byte 3 are masked off; extremes of both counts
  const uint8_t junk[4] = {0x3F, 0xFF, 0xFF, 0xFF};
  CHECK(decode(junk).raw_pressure == 16383);
  CHECK(decode(junk).raw_temperature == 2047);
  const uint8_t zero[4] = {0x00, 0x00, 0x00, 0x1F};
  CHECK(decode(zero).raw_pressure == 0);
  CHECK(decode(zero).raw_temperature == 0);

  // datasheet digital pressure table (p6): % of full scale -> counts
  const struct { float pct; uint16_t counts; } ptab[] = {
      {0, 1000}, {5, 1700}, {10, 2400}, {50, 8000}, {90, 13600}, {95, 14300}, {100, 15000}};
  for (auto &row : ptab) CHECK_NEAR(counts_to_pressure(row.counts, 1000, 15000, 0, 100), row.pct, 1e-3f);

  // compound part: -14.7 .. 200 psi
  CHECK_NEAR(counts_to_pressure(1000, 1000, 15000, -14.7f, 200), -14.7f, 1e-4f);
  CHECK_NEAR(counts_to_pressure(15000, 1000, 15000, -14.7f, 200), 200.0f, 1e-3f);

  // datasheet digital temperature table (p7): counts -> C (table rounds to whole degrees)
  const struct { uint16_t counts; float c; } ttab[] = {{512, 0}, {614, 10}, {767, 25}, {921, 40}, {1075, 55}};
  for (auto &row : ttab) CHECK_NEAR(counts_to_temperature(row.counts), row.c, 0.2f);
  CHECK_NEAR(counts_to_temperature(0), -50.0f, 1e-4f);

  // invalid ranges give NAN, never a number
  CHECK(isnan(counts_to_pressure(8000, 1000, 1000, 0, 100)));                  // span == zero
  CHECK(isnan(counts_to_pressure(8000, 15000, 1000, 0, 100)));                 // span < zero
  CHECK(isnan(counts_to_pressure(8000, 1000, 15000, 100, 100)));               // max == min
  CHECK(isnan(counts_to_pressure(8000, 1000, 15000, 0, NAN)));                 // NAN range
  CHECK(isnan(counts_to_pressure(8000, 1000, 16384, 0, 100)));                 // span beyond 14 bits

  // plausibility window
  CHECK(in_window(250, 250, 15750) && in_window(15750, 250, 15750));
  CHECK(!in_window(249, 250, 15750) && !in_window(15751, 250, 15750));
  CHECK(in_window(0, 0, 16383) && in_window(16383, 0, 16383));

  return tests_done("test_decode");
}

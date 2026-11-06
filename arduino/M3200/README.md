# M3200 Arduino library

Driver for the TE Connectivity **M3200** pressure transducer, I2C output variant (`M32J…`).
It gives you 14-bit pressure, 11-bit temperature and the sensor's status bits.

## Install

Copy the `M3200` folder into your Arduino `libraries` folder, or in PlatformIO add
`lib_deps = file://path/to/M3200`.

## Wiring

| Wire  | Signal | Notes |
|-------|--------|-------|
| Red   | VCC    | 2.7–5.0 V |
| Black | GND    | |
| Green | SDA    | 4.7 kΩ pull-up |
| White | SCL    | 4.7 kΩ pull-up |

## Usage

```cpp
#include <Wire.h>
#include <M3200.h>

M3200 sensor(100);            // 0..100 psi part (M32JM-000105-100PG)

void setup() {
  Wire.begin(23, 22);         // you start Wire, so you can pick the pins
  sensor.begin();             // Wire, address 0x28 by default
}

void loop() {
  if (sensor.read() == M3200_OK) {
    Serial.println(sensor.pressure());     // in the unit of the range you gave
    Serial.println(sensor.temperature());  // °C
  }
  delay(1000);
}
```

### API

| Call | What it does |
|------|--------------|
| `M3200(pMax, pMin = 0)` | Rated range in any unit. Compound part: `M3200(200, -14.7)` |
| `begin(wire, address)` | Stores the bus and does a dummy fetch. Returns `false` if the sensor doesn't answer, or answers with a fault or reserved status. `lastStatus()` says which |
| `read()` | Sends a measurement request first (sleep mode only), then calls `fetch()` |
| `fetch()` | Reads the 4-byte Read_DF4 packet. Never blocks |
| `requestMeasurement()` | Measurement request for sleep-mode parts (sent as a Write_MR, see the main README). Call `fetch()` at least 15 ms later |
| `pressure()`, `temperature()` | Values from the last read. `NAN` until the first good read, and after a fault, reserved status or bus error, so an old value never passes as a current one |
| `isValid()` | `true` when the last read returned usable data (ok or stale) |
| `rawPressure()`, `rawTemperature()` | Raw counts |
| `staleCount()` | Stale packets in a row. At `M3200_STALE_WARNING_COUNT` (5) the values are frozen, usually because `setSleepMode()` doesn't match the part |
| `setSleepMode(bool)` | On by default and works with both part types. `false` is only for parts with sleep digit = 0, and saves the wait in each `read()` |
| `setMeasurementDelay(ms)` | Wait between the request and the fetch in `read()`. Default 15 ms (8.4 ms max per datasheet, plus margin) |
| `setValidCountRange(min, max)` | Plausibility window for the raw pressure count, default 250–15750 (about −5 % to +105 % of full scale). A count outside it returns `M3200_OUT_OF_RANGE` and `NAN` values, with `rawPressure()` still set. `(0, 16383)` turns the check off |
| `setCountRange(zero, span)` | Trims an individual unit (defaults 1000 / 15000). Returns `false` and changes nothing unless zero < span ≤ 16383 |
| `setPressureRange(max, min)` | Changes the range after construction. Returns `false` and changes nothing unless max > min. An invalid range given to the constructor makes `pressure()` read `NAN` |

`read()` and `fetch()` return an `M3200Status`:

| Value | Meaning | Values updated? |
|-------|---------|-----------------|
| `M3200_OK` | Good packet | yes |
| `M3200_STALE` | Already fetched since the last conversion (you're polling too fast) | yes (same data) |
| `M3200_FAULT` | Sensor fault. Stays latched until power-cycle | no |
| `M3200_RESERVED` | Reserved status code | no |
| `M3200_I2C_ERROR` | No ACK or short read | no |

## Conversions (datasheet p6–7)

```
P = (counts − 1000) × (Pmax − Pmin) / 14000 + Pmin
T = counts11 × 200 / 2048 − 50      [°C]
```

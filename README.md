# M3200 pressure sensor drivers

[![CI](https://github.com/jbuck2005/te-m3200/actions/workflows/ci.yml/badge.svg)](https://github.com/jbuck2005/te-m3200/actions/workflows/ci.yml)

Drivers for the TE Connectivity **M3200** pressure transducer with I2C output (`M32J…` parts).
The M3200 is a one-piece stainless-steel transducer for water, oil, gas and other media.
The digital version reports 14-bit pressure and 11-bit temperature over I2C, at 2.7–5.0 V.

The original code was written in C++ for the Arduino framework in 2020 and did all of the computation as functions.
Two sensors originally used SNMP to report their values to an InfluxDB / Grafana stack, but I wanted to migrate this to ESPHome.

| Folder | What it is |
|--------|------------|
| [`arduino/M3200`](arduino/M3200) | Arduino library (any architecture with `Wire`) |
| [`esphome`](esphome) | ESPHome external component (`platform: m3200`) and example configs |
| [`docs`](docs) | Reference copies of the TE datasheets (see [Datasheets](#datasheets) for the live links) |

Both drivers read the sensor the same way: an optional measurement request, a 4-byte data
fetch, a status check, then the conversion to pressure and °C. [How the sensor talks](#how-the-sensor-talks)
below walks through it byte by byte.

## Quick start: ESPHome

```yaml
external_components:
  - source: github://jbuck2005/te-m3200@v1.0.0
    components: [m3200]

i2c:
  sda: GPIO21
  scl: GPIO22

sensor:
  - platform: m3200
    max_pressure: 100        # top of your part's range
    # sleep_mode: false      # optional if the part number's sleep digit is 0 (see below)
    pressure:
      name: "Pressure"
    temperature:
      name: "Sensor Temperature"
```

See [`esphome/README.md`](esphome/README.md) for all the options, and
[`esphome/full_example.yaml`](esphome/full_example.yaml) for a complete device that reports
to Home Assistant and MQTT.

## Quick start: Arduino

```cpp
#include <Wire.h>
#include <M3200.h>

M3200 sensor(100);            // 0..100 psi part

void setup() {
  Serial.begin(115200);
  Wire.begin();
  // sensor.setSleepMode(false); // optional if the part number's sleep digit is 0 (see below)
  sensor.begin();
}

void loop() {
  if (sensor.read() == M3200_OK) {
    Serial.print(sensor.pressure());
    Serial.print(" psi, ");
    Serial.print(sensor.temperature());
    Serial.println(" °C");
  }
  delay(1000);
}
```

See [`arduino/M3200/README.md`](arduino/M3200/README.md) for the full API and the examples.

## Wiring

| Signal | Cable | M12 pin |
|--------|-------|---------|
| VCC (2.7–5.0 V) | Red   | 1 |
| GND             | Black | 3 |
| SDA             | Green | 4 |
| SCL             | White | 2 |

SDA and SCL each need a pull-up to VCC. 4.7 kΩ suits most setups. On a long cable, keep both
lines the same length so their loads match. The bus runs at 100–400 kHz.
Sources: M3200 datasheet p4 (wiring), Interfacing guide §1.1 and p6 (pull-ups, timing).

## Reading the part number

The digital ordering code tells you the settings the drivers need (M3200 datasheet p10):

```
M32 J M - 00 0 1 0 5 - 100P G
    │ │      │ │ │ │   │    └─ pressure type: G = gage (0..max), C = compound (-14.7 psi..max)
    │ │      │ │ │ │   └────── range: 100P = 100 psi, 007B = 7 bar, ...
    │ │      │ │ │ └────────── pressure port: 5 = 1/4-18 NPT, B = G1/4, ...
    │ │      │ │ └──────────── I2C address: 0=0x28  1=0x36  2=0x46  3=0x48  4=0x51  (3, 4: see below)
    │ │      │ └────────────── sleep mode: 0 = off, 1 = on
    │ │      └──────────────── snubber: 0 = none, 1 = fitted (G1/4 port only)
    │ └─────────────────────── connection: L = 0.5 m cable, M = 1 m cable, D = M12
    └───────────────────────── output: J = I2C
```

That's the example part used throughout this repo, `M32JM-000105-100PG`: 1 m cable, no snubber, sleep mode on,
address 0x28, 1/4" NPT, 0–100 psi gage. So it needs `max_pressure: 100`, with `sleep_mode` left on.

Sleep mode is on by default in both drivers because it works with either part type. A
non-sleep part (sleep digit 0) measures continuously and returns fresh data after the
request anyway. Turning `sleep_mode` off for one only saves the wait (15 ms by default) per read.
Turning it off for a sleep-mode part freezes the readings (see [Status bits](#4-status-bits)).

> **The two TE documents disagree on the address codes.** The datasheet's ordering table (p10)
> lists five codes: `0`–`4` = 0x28, 0x36, 0x46, 0x48, 0x51. The Interfacing guide (§1.2) names
> only three factory addresses, 0x28, 0x36 and 0x46, and gives their read headers (address
> shifted left, read bit set) as 0x51, 0x6D and 0x8D. 0x51 is exactly the read header for 0x28
> (`0x28 << 1 | 1`), so code `4` looks like a header that ended up in the address column, and
> nothing in the guide backs up code `3` (0x48) either. Codes `0`–`2` agree in both documents. If you
> order code `3` or `4`, confirm the address with TE or run an I2C bus scan when the part arrives.

## How the sensor talks

The M3200's digital output comes from a TE sensor conditioning chip (SSC). It's the same
interface as other TE digital pressure parts (MS45x5DO, 86BSD, MSP300, FX29, …). The full
description is TE's *Interfacing to Digital Pressure Modules* guide, also appended to the
M3200 datasheet as pages 11–22.

### 1. Addressing

The 7-bit I2C address is set at the factory by the ordering code (0x28 by default). There are
no registers and nothing to configure. Every command is just the address byte followed by
reads (Interfacing guide §1.2, §1.6).

### 2. Measurement request (sleep-mode parts only)

A sleep-mode part draws 5 µA and does nothing until asked. A **Read_MR** command wakes it
and starts one conversion. Read_MR is the address byte alone, followed straight away by a
stop condition, with no data bytes (figure 1.6, example 1). The result is ready after
**8.4 ms** (M3200 datasheet p3), so the drivers wait 15 ms by default before fetching (configurable), which leaves margin for clock tolerance. A non-sleep part
converts continuously and needs no request.

The guide's figure shows Read_MR with the read bit set. These drivers send it as an
address-only write: `Wire.beginTransmission()` / `endTransmission()` in Arduino, a
zero-length `write()` in ESPHome. The M3200 starts a conversion either way, and that's what
the standard I2C APIs can send.

### 3. Data fetch: Read_DF4

The driver reads 4 bytes (figure 1.6, example 4; Interfacing guide §1.6.1):

```
          byte 0                 byte 1               byte 2                 byte 3
 ┌─────┬──────────────┐ ┌──────────────────┐ ┌──────────────────┐ ┌──────────┬───────────┐
 │S1 S0│ B13 ...  B8  │ │ B7  ...       B0 │ │ T10 ...       T3 │ │ T2 T1 T0 │ x x x x x │
 └─────┴──────────────┘ └──────────────────┘ └──────────────────┘ └──────────┴───────────┘
 status  bridge (pressure) data, 14 bits      temperature, 11 bits           ignore
```

Shorter fetches also exist: 2 bytes (pressure only) or 3 bytes (8-bit temperature). The
drivers always use DF4 for full temperature resolution. Decoding is a few shifts and masks:

```c
status   =  b0 >> 6;                       // top 2 bits
pressure = ((b0 & 0x3F) << 8) | b1;        // 14 bits, 0..16383
temp     = (b2 << 3) | (b3 >> 5);          // 11 bits, 0..2047
```

### 4. Status bits

| S1 S0 | Meaning | What the drivers do |
|-------|---------|---------------------|
| `00` | Normal: a fresh measurement | Use it |
| `01` | Reserved | Discard the reading |
| `10` | **Stale**: already fetched since the last conversion | Values are still valid, just not new. Keep them, but this means you're polling faster than the sensor converts |
| `11` | **Fault** in the conditioner or sensing element | Report a fault. It stays latched until the cause is fixed **and** the sensor is power-cycled |

Source: Interfacing guide §1.7, table 1. A sleep-mode part read without a measurement
request returns `10` (stale) forever. That's the most common sign that `sleep_mode` has been
turned off for a part that needs it. After 5 stale readings in a row, both drivers flag the values as frozen.

### 5. Converting counts

Pressure (M3200 datasheet p6). Zero pressure reads 1000 counts and full scale reads 15000,
so 14000 counts span the range:

```
P = (counts − 1000) × (Pmax − Pmin) / 14000 + Pmin
```

Temperature (M3200 datasheet p7). The 11 bits span −50 °C to +150 °C:

```
T = counts × 200 / 2048 − 50      [°C]
```

As a check against the datasheet's own table, 767 counts gives 24.9 °C (the table says 25 °C).

The temperature is the pressure port diaphragm's, and it's only specified to **±3 °C** over the
compensated range of **0–55 °C** (datasheet p3, note 2). Readings outside that range are
uncompensated. The 0.1 °C resolution of the 11-bit count is much finer than the accuracy, so
treat the temperature as a rough indication, not a calibrated measurement.

The datasheet allows each unit's endpoints to vary around the typical 1000 / 15000 counts (p3). To trim a
unit against a reference gauge, set `zero_counts` / `span_counts` (ESPHome) or call `setCountRange()`
(Arduino).

#### Datasheet revisions and the endpoint tolerances

Datasheet rev A21 (06/2026) narrowed the allowed endpoint spread compared with rev A20 (11/2023):

| p3 | Rev A20 (11/2023) | Rev A21 (06/2026) | Typical (both) |
|----|-------------------|-------------------|----------------|
| Output at zero pressure | 750–1250 | 790–1210 | 1000 |
| Output at full-scale pressure | 14720–15250 | 14790–15210 | 15000 |

**The drivers deliberately keep the wider A20 limits.** Here's why:

- **Older sensors are still in the field.** A unit built to A20 can legitimately read, say, 760 counts
  at zero. That's in spec for its build, but outside the A21 window.
- **The limits only drive a warning.** The ESPHome config check warns when `zero_counts` / `span_counts`
  falls outside the tolerance, to catch typos like `100` for `1000`. It never rejects a config. With the
  A21 numbers, trimming an older unit to its real endpoint would trigger a false warning. The wider window
  catches typos just as well.
- **Nothing else depends on them.** The conversion uses the typical 1000 / 15000, which didn't change.
  The runtime plausibility window (`min_valid_counts` / `max_valid_counts`, default 250–15750) is much
  wider than either revision, so readings are identical whichever limits are chosen.
- **Tightening would buy nothing.** A sensor that drifts slightly past the A21 limit is a calibration
  question for a reference gauge, not something the config check can judge.

If you change `ZERO_COUNTS_TOLERANCE` / `SPAN_COUNTS_TOLERANCE` in `esphome/components/m3200/sensor.py`,
keep this table and reasoning in step. A future datasheet revision that changes the *typical* values
(1000 / 15000) would be a different matter, because the conversion depends on those.

### Worked example

A 0–100 psi gage part at 50 psi and 16.7 °C returns `1F 40 55 60`:

| Step | Value |
|------|-------|
| status = `0x1F >> 6` | `00`: normal |
| pressure counts = `(0x1F & 0x3F) << 8 \| 0x40` | `0x1F40` = 8000 |
| P = (8000 − 1000) × 100 / 14000 | **50.0 psi** |
| temp counts = `0x55 << 3 \| 0x60 >> 5` | 680 + 3 = 683 |
| T = 683 × 200 / 2048 − 50 | **16.7 °C** |

If the same packet came back with byte 0 = `9F` instead, the status would be `10`, meaning
the same 50.0 psi as last time, not a new measurement.

### Gotchas

- **The sample C code in the datasheet (pp18–22) leaves out the zero offset.** It computes
  `counts × range / 14000` without subtracting 1000 first, so its readings come out
  1000 counts (about 7% of full scale) too high. Use the formula above.
- **Polling faster than the sensor converts gives stale packets.** They're harmless, just
  repeats. TE recommends polling at least 20% slower than the update time (Interfacing guide §1.6.1).
- **The I2C implementation has quirks** (Interfacing guide §1.8). A START immediately followed
  by STOP with no clock pulses, or a repeated START, makes the *next* transaction fail.
  Normal `Wire` / ESPHome transactions avoid both.

## Versions

Releases are git tags (`v1.0.0`, …). Pin one in `external_components` (`@v1.0.0` or `ref: v1.0.0`) instead of
`@main`. With `@main`, every push to this repo, including an unfinished or mistaken one, ends up in your
device's next build. Tags follow semantic versioning: `v1.0.x` for fixes, `v1.x.0` for new options, and
`v2.0.0` if existing YAML stops working. The Arduino library's version is in
`arduino/M3200/library.properties`.

## Tests

`tests/run.sh` runs host-side tests on a PC, with no board, no sensor and no CI needed (just `g++` or
`clang++`):

- `tests/test_decode.cpp` checks the shared `m3200_decode.h` against the datasheet's pressure and
  temperature tables, the worked example above, every status value, junk bits, the extreme counts,
  invalid ranges and the plausibility window.
- `tests/test_arduino.cpp` runs the Arduino library against a fake `Wire` (`tests/fakes/`):
  stale counting, fault/reserved/bus-error handling (`NAN` values), range setters and `begin()`.
- It also checks that the two copies of `m3200_decode.h` (Arduino library and ESPHome component)
  are identical.

Run it before pushing changes to either driver.

[CI](.github/workflows/ci.yml) runs it on every pull request and push to `main`, together with
`ruff check`, an Arduino build of both examples for `uno` and `esp32dev`, and an ESPHome compile of
[`tests/esphome`](tests/esphome) on ESP32 (ESP-IDF and Arduino), ESP8266 and RP2040. The ESPHome
tests build the component from the checkout, so a PR is tested with its own code. A weekly run
also builds against the ESPHome beta, to catch an ESPHome change before it reaches a device.

To build locally: `esphome compile tests/esphome/test.esp32-idf.yaml`, and
`pio ci arduino/M3200/examples/BasicRead/BasicRead.ino --lib arduino/M3200 --board uno`.

## Datasheets

The manufacturer's documents are the reference. Use the current versions from TE Connectivity:

- **[M3200 datasheet](https://www.te.com/commerce/DocumentDelivery/DDEController?Action=srchrtrv&DocNm=M3200&DocType=Data%20Sheet&DocLang=English&DocFormat=pdf&PartCntxt=20006465-00)** (PDF, te.com, always the latest revision): specs, count ranges,
  response times, wiring, ordering codes, plus the interfacing guide and sample C code as an appendix (pp11–22).
- **Interfacing to Digital Pressure Modules** (TE application note, rev A3 12/2016): I2C/SPI commands,
  packet layout, status bits, timing. The datasheet now includes this material as its own interfacing
  section (pp11–17, with the same §1.1–§2.1 numbering), so the "Interfacing guide §…" references in this
  README can be followed in either document. The datasheet (note 6) still points to the standalone note
  as *Interfacing to MEAS Digital Pressure Modules*.

TE has revised these documents several times, so `docs/` keeps copies of the versions this code was written against:

- [M3200 datasheet, rev A21](docs/ENG_DS_M3200_A21.pdf) (06/2026)
- [M3200 datasheet, rev A20](docs/ENG_DS_M3200_A20-1489351.pdf)
- [Interfacing to Digital Pressure Modules, rev A3](docs/Interfacing_to_DigitalPressure_Modules_A3.pdf) (12/2016)

These PDFs are TE Connectivity's documents, kept for reference. They aren't covered by this repo's MIT licence.

Page and section numbers in this README and in the code refer to datasheet rev A21 (06/2026). Its
pages match rev A20. The only technical change between the two is the tighter zero/full-scale count tolerance (see [Datasheet revisions and the endpoint tolerances](#datasheet-revisions-and-the-endpoint-tolerances)). They
also refer to Interfacing guide rev A3.

## License

The code is under the [MIT](LICENSE) license. The datasheets in `docs/` are TE Connectivity's.

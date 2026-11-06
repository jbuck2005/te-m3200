# M3200 ESPHome external component

Sensor platform for the TE Connectivity **M3200** I2C pressure transducer (`M32J…`).

## Install

Add this to your device YAML. ESPHome downloads the component from GitHub when it builds:

```yaml
external_components:
  - source: github://jbuck2005/te-m3200@v1.0.0
    components: [m3200]
```

Pin a release tag like `@v1.0.0` rather than `@main`. With `@main`, every push to this repo goes into your next build (see [Versions](../README.md#versions)).

To use a local copy instead, put the `components/m3200` folder next to your YAML and use
`source: {type: local, path: components}`.

## Configuration

```yaml
i2c:
  sda: GPIO21
  scl: GPIO22

sensor:
  - platform: m3200
    max_pressure: 100
    update_interval: 5s
    pressure:
      name: "Pressure"
    temperature:
      name: "Sensor Temperature"
```

| Option | Default | Description |
|--------|---------|-------------|
| `max_pressure` | **required** | Upper end of the part's rated range |
| `min_pressure` | `0` | Lower end. Use `-14.7` for compound (`…C`) parts |
| `sleep_mode` | `true` | Sends a measurement request and fetches `measurement_delay` later without blocking. Works with both part types. `false` is only for parts with sleep digit = 0, and skips the wait |
| `measurement_delay` | `15ms` | Wait between the measurement request and the fetch (sleep mode only). The datasheet gives 8.4 ms max at the nominal clock, so 9 ms is the minimum allowed |
| `max_failures` | `3` | After this many reads in a row without usable data (no answer, short read, reserved status), publish `NAN` ("unknown") so Home Assistant stops showing the last value. `0` = never |
| `min_valid_counts` / `max_valid_counts` | `250` / `15750` | Plausibility window for the raw pressure count (about −5 % to +105 % of full scale). A count outside it is almost always a wiring, supply or over-range problem: the reading is discarded, `NAN` is published and the status reads `out of range`. `0` / `16383` turns the check off |
| `zero_counts` / `span_counts` | `1000` / `15000` | Count endpoints, for trimming an individual unit. A value outside the datasheet tolerance (750–1250 / 14720–15250, the wider rev A20 limits on purpose) logs a warning but is accepted. See the main README for why |
| `address` | `0x28` | `0x36`, `0x46`, `0x48` or `0x51` depending on ordering code |
| `update_interval` | `60s` | |
| `raw_pressure` | | Optional diagnostic sensor: the 14-bit pressure count before conversion (1000 = 0 %, 15000 = 100 %). Useful for spotting wiring or range problems |
| `status` | | Optional diagnostic text sensor: `ok`, `stale`, `frozen`, `fault`, `reserved`, `out of range` or `no answer`. Published only when it changes |
| `pressure` / `temperature` | | Standard sensor options. Pressure defaults to `psi`. If your range is in bar, set `unit_of_measurement: bar`. A build warns when the pressure unit isn't set explicitly |

The temperature is the diaphragm's, accurate to ±3 °C over 0–55 °C only (datasheet p3). It's
shown with 1 decimal place by default; set `accuracy_decimals: 0` if you'd rather not suggest
more accuracy than the sensor has.

Averaging, deltas and throttling are done with ESPHome's own sensor `filters:`
(`sliding_window_moving_average`, `delta`, `throttle`, …). The component doesn't do any of its own.

Status handling:
- **stale**: in sleep mode, a stale packet right after a measurement request is re-fetched once,
  5 ms later, in case the conversion was slow. Otherwise it's published anyway (the data is valid,
  just not new) and logged at debug level. After 5 in a row the values are frozen, usually because
  `sleep_mode` doesn't match the part number. The component logs one warning, publishes `NAN`
  once and then publishes nothing until fresh data arrives, so frozen values don't end up in
  Home Assistant's long-term statistics. The component warning stays set until then.
- **fault**: publishes `NAN` and sets the component warning. The fault stays latched until the sensor is power-cycled.
- **I2C error / reserved status**: nothing is published at first, and the component warning is set. After `max_failures` in a row (default 3), `NAN` is published so the value shows as unknown instead of frozen. A bus error is logged once per outage, and again when the sensor answers.
- **No answer at boot**: the component keeps trying every `update_interval` instead of giving up until the next reboot, so a sensor plugged in late starts working on its own.

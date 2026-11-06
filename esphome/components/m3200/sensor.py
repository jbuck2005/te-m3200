# ********************************************************************************************************************
# M3200 SENSOR PLATFORM
# ********************************************************************************************************************
# sensor.py - turns the YAML under "- platform: m3200" into C++ for the M3200Component (m3200.h / m3200.cpp)
#
# Initial coding: 20261101        jbuck2005
#
# ESPHome runs this file on your PC (or in Home Assistant) when it compiles - it never runs on the ESP32:
#   1. CONFIG_SCHEMA checks the YAML options and fills in the defaults
#   2. to_code() writes the C++ that creates the component and calls its set_...() functions

import logging                                                                  # for config-time warnings

import esphome.codegen as cg                                                    # writes the C++ code
from esphome.components import i2c, sensor, text_sensor                                      # helpers for the i2c bus and for sensor entities
import esphome.config_validation as cv                                          # YAML checks (cv.float_, cv.boolean ...)
from esphome.const import (                                                     # option names and units shared by all ESPHome components
    CONF_ID,                                                                    # the component's id:
    CONF_PRESSURE,                                                              # "pressure"
    CONF_TEMPERATURE,                                                           # "temperature"
    DEVICE_CLASS_PRESSURE,                                                      # tells Home Assistant this is a pressure
    CONF_STATUS,                                                                # "status"
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,                                                 # listed under Diagnostic in HA                                                   # tells Home Assistant this is a temperature
    STATE_CLASS_MEASUREMENT,                                                    # tells Home Assistant to keep long-term statistics
    CONF_UNIT_OF_MEASUREMENT,                                                   # "unit_of_measurement"
    UNIT_CELSIUS,                                                               # "°C"
)

DEPENDENCIES = ["i2c"]                                                          # the YAML must also have an i2c: block
AUTO_LOAD = ["text_sensor"]                                                     # for the optional status: entity

# ********************************************************************************************************************
# YAML OPTIONS
# ********************************************************************************************************************
CONF_MIN_PRESSURE = "min_pressure"                                              # option names this component adds
CONF_MAX_PRESSURE = "max_pressure"
CONF_SLEEP_MODE = "sleep_mode"
CONF_MEASUREMENT_DELAY = "measurement_delay"
CONF_MAX_FAILURES = "max_failures"
CONF_RAW_PRESSURE = "raw_pressure"
CONF_MIN_VALID_COUNTS = "min_valid_counts"
CONF_MAX_VALID_COUNTS = "max_valid_counts"
CONF_ZERO_COUNTS = "zero_counts"
CONF_SPAN_COUNTS = "span_counts"

COUNT_RANGE = cv.int_range(0, 16383)                                            # any 14-bit count (2^14 - 1 = 16383)
# Endpoint tolerances used ONLY for the config-time "is this a typo?" warning on zero_counts / span_counts.
# These are deliberately the WIDER datasheet rev A20 (11/2023) limits, not the tighter rev A21 (06/2026) ones:
#   rev A20: zero 750..1250,  full scale 14720..15250   <- used here
#   rev A21: zero 790..1210,  full scale 14790..15210   (TE narrowed the tolerance for newly built units)
# Why: units built under A20 can legitimately read e.g. 760 at zero. Someone trimming such a unit against a
# reference gauge would get a false warning with the A21 numbers. The warning exists to catch typos (e.g. 100
# instead of 1000), so the wider window does that just as well and covers every unit in the field.
# Nothing else depends on these: conversion uses the typical 1000 / 15000 (unchanged in A21) and the runtime
# plausibility window (min/max_valid_counts, 250..15750) is far wider than either revision.
# See README "Datasheet revisions and the endpoint tolerances". Do not "tighten" these to A21 without reading it.
ZERO_COUNTS_TOLERANCE = (750, 1250)                                             # datasheet A20 p3: output at zero pressure (A21: 790..1210)
SPAN_COUNTS_TOLERANCE = (14720, 15250)                                          # datasheet A20 p3: output at full scale (A21: 14790..15210)

_LOGGER = logging.getLogger(__name__)                                           # prints during "esphome config / compile"

m3200_ns = cg.esphome_ns.namespace("m3200")                                     # the C++ namespace esphome::m3200
M3200Component = m3200_ns.class_(                                               # the C++ class esphome::m3200::M3200Component
    "M3200Component", cg.PollingComponent, i2c.I2CDevice                        # ...which runs on a timer and talks I2C
)


def _validate_ranges(config):                                                   # checks that need more than one option
    if config[CONF_MAX_PRESSURE] <= config[CONF_MIN_PRESSURE]:                  # e.g. max 100, min 200 makes no sense
        raise cv.Invalid(f"{CONF_MAX_PRESSURE} must be greater than {CONF_MIN_PRESSURE}")
    if config[CONF_SPAN_COUNTS] <= config[CONF_ZERO_COUNTS]:                    # same for the count endpoints
        raise cv.Invalid(f"{CONF_SPAN_COUNTS} must be greater than {CONF_ZERO_COUNTS}")
    if config[CONF_MAX_VALID_COUNTS] < config[CONF_MIN_VALID_COUNTS]:
        raise cv.Invalid(f"{CONF_MAX_VALID_COUNTS} must not be less than {CONF_MIN_VALID_COUNTS}")
    for key, (low, high) in (                                                   # legal but outside the datasheet tolerance: probably a typo
        (CONF_ZERO_COUNTS, ZERO_COUNTS_TOLERANCE),
        (CONF_SPAN_COUNTS, SPAN_COUNTS_TOLERANCE),
    ):
        if not low <= config[key] <= high:
            _LOGGER.warning(
                "m3200: %s: %d is outside the datasheet tolerance %d..%d - check it's not a typo",
                key, config[key], low, high,
            )
    return config                                                               # all good - hand the config back unchanged


def _check_pressure_unit(config):                                               # runs on the YAML as written, before defaults are filled in
    pressure = config.get(CONF_PRESSURE) if isinstance(config, dict) else None
    if isinstance(pressure, dict) and CONF_UNIT_OF_MEASUREMENT not in pressure:
        _LOGGER.warning(
            "m3200: pressure has no unit_of_measurement, so it defaults to psi. "
            "If max_pressure / min_pressure are in another unit (bar, kPa ...), set unit_of_measurement to match"
        )
    return config                                                               # never changes or rejects anything


CONFIG_SCHEMA = cv.All(                                                         # the YAML must pass _check_pressure_unit, the schema AND _validate_ranges
    _check_pressure_unit,
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(M3200Component),                     # makes an id if you don't give one
            cv.Required(CONF_MAX_PRESSURE): cv.float_,                          # top of the rated range, in your unit (psi)
            cv.Optional(CONF_MIN_PRESSURE, default=0.0): cv.float_,             # bottom of the range (-14.7 for compound)
            cv.Optional(CONF_SLEEP_MODE, default=True): cv.boolean,             # works for both part types; false saves the wait on non-sleep parts
            cv.Optional(CONF_MEASUREMENT_DELAY, default="15ms"): cv.All(       # wait between request and fetch (8.4 ms max, datasheet p3)
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(milliseconds=9), max=cv.TimePeriod(milliseconds=500)),
            ),
            cv.Optional(CONF_MAX_FAILURES, default=3): cv.int_range(0, 255),     # reads in a row without data before "unknown" (0 = never)
            cv.Optional(CONF_MIN_VALID_COUNTS, default=250): COUNT_RANGE,       # plausibility window: counts outside it are discarded
            cv.Optional(CONF_MAX_VALID_COUNTS, default=15750): COUNT_RANGE,     #   (about -5 %..+105 % of full scale; 0 / 16383 = off)
            cv.Optional(CONF_ZERO_COUNTS, default=1000): COUNT_RANGE,           # count at min pressure (datasheet p3)
            cv.Optional(CONF_SPAN_COUNTS, default=15000): COUNT_RANGE,          # count at max pressure (datasheet p3)
            cv.Optional(CONF_PRESSURE): sensor.sensor_schema(                   # the pressure: entity
                unit_of_measurement="psi",                                      # default unit; override in YAML for bar
                accuracy_decimals=1,                                            # show 1 decimal place
                device_class=DEVICE_CLASS_PRESSURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_RAW_PRESSURE): sensor.sensor_schema(               # diagnostic: the 14-bit count before conversion
                unit_of_measurement="counts",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:counter",
            ),
            cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(           # diagnostic: ok / stale / frozen / fault / reserved / out of range / no answer
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:list-status",
            ),
            cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(                # the temperature: entity
                unit_of_measurement=UNIT_CELSIUS,                               # always °C
                accuracy_decimals=1,                                            # show 1 decimal place
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))                                 # adds update_interval: (default 60s)
    .extend(i2c.i2c_device_schema(0x28)),                                       # adds address: (default 0x28) and i2c_id:
    _validate_ranges,
)


# ********************************************************************************************************************
# CODE GENERATION
# ********************************************************************************************************************
async def to_code(config):                                                      # writes the C++ for one "- platform: m3200" block
    var = cg.new_Pvariable(config[CONF_ID])                                     # -> new M3200Component()
    await cg.register_component(var, config)                                    # ESPHome will call its setup() / update()
    await i2c.register_i2c_device(var, config)                                  # connects it to the i2c bus + address

    cg.add(                                                                     # -> var->set_pressure_range(min, max);
        var.set_pressure_range(config[CONF_MIN_PRESSURE], config[CONF_MAX_PRESSURE])
    )
    cg.add(                                                                     # -> var->set_count_range(zero, span);
        var.set_count_range(config[CONF_ZERO_COUNTS], config[CONF_SPAN_COUNTS])
    )
    cg.add(                                                                     # -> var->set_valid_count_range(250, 15750);
        var.set_valid_count_range(config[CONF_MIN_VALID_COUNTS], config[CONF_MAX_VALID_COUNTS])
    )
    cg.add(var.set_sleep_mode(config[CONF_SLEEP_MODE]))                         # -> var->set_sleep_mode(true/false);
    cg.add(var.set_max_failures(config[CONF_MAX_FAILURES]))                     # -> var->set_max_failures(3);
    cg.add(var.set_measurement_delay(config[CONF_MEASUREMENT_DELAY].total_milliseconds))  # -> var->set_measurement_delay(15);

    if pressure_config := config.get(CONF_PRESSURE):                            # only if pressure: is in the YAML
        sens = await sensor.new_sensor(pressure_config)                         # create the entity
        cg.add(var.set_pressure_sensor(sens))                                   # and hand it to the component
    if temperature_config := config.get(CONF_TEMPERATURE):                      # only if temperature: is in the YAML
        sens = await sensor.new_sensor(temperature_config)                      # create the entity
        cg.add(var.set_temperature_sensor(sens))                                # and hand it to the component
    if raw_config := config.get(CONF_RAW_PRESSURE):                             # only if raw_pressure: is in the YAML
        sens = await sensor.new_sensor(raw_config)
        cg.add(var.set_raw_pressure_sensor(sens))
    if status_config := config.get(CONF_STATUS):                                # only if status: is in the YAML
        sens = await text_sensor.new_text_sensor(status_config)
        cg.add(var.set_status_text_sensor(sens))

/*    m3200.h - ESPHome sensor component for the TE Connectivity M3200 digital (I2C) pressure transducer
 *
 *    Initial coding: 20261101        jbuck2005      (ported from the Arduino library in arduino/M3200)
 *
 *    How ESPHome uses this:
 *      1. sensor.py reads the "- platform: m3200" options from your YAML and calls the set_...() functions below
 *      2. ESPHome calls setup() once at boot
 *      3. ESPHome calls update() every update_interval, which reads the sensor and publishes the values
 *
 *    See arduino/M3200/src/M3200.h, or the README, for the packet layout and wiring.
 */
#pragma once                                                                    // only include this file once per compile

#include "esphome/components/i2c/i2c.h"                                         // I2CDevice: read() / write() on the configured bus + address
#include "esphome/components/sensor/sensor.h"                                   // sensor::Sensor: the entities that publish values
#include "esphome/components/text_sensor/text_sensor.h"                         // text_sensor::TextSensor: the optional status entity
#include "esphome/core/component.h"                                             // PollingComponent: calls update() on a timer

namespace esphome {
namespace m3200 {

/*********************************************************************************************************************
 * STATUS CODES
 ********************************************************************************************************************/
enum M3200Status : uint8_t {                                                    // the two status bits [15:14] at the top of the first data byte
  M3200_STATUS_OK = 0,                                                          // 0b00 normal operation, fresh data
  M3200_STATUS_RESERVED = 1,                                                    // 0b01 reserved by TE - treated as an error
  M3200_STATUS_STALE = 2,                                                       // 0b10 already fetched since the last measurement (valid, just not new)
  M3200_STATUS_FAULT = 3,                                                       // 0b11 sensor fault - latched until the sensor is power cycled
};                                                                              // end of M3200Status

/*********************************************************************************************************************
 * M3200 COMPONENT
 ********************************************************************************************************************/
class M3200Component : public PollingComponent, public i2c::I2CDevice {         // runs on a timer + talks I2C
 public:                                                                        // called by ESPHome
  void setup() override;                                                        // once at boot: check the sensor answers
  void dump_config() override;                                                  // once at boot: print the settings to the log
  void update() override;                                                       // every update_interval: take a reading
  float get_setup_priority() const override { return setup_priority::DATA; }    // start after the I2C bus is up

  // called by the code sensor.py generates from your YAML
  void set_pressure_sensor(sensor::Sensor *sensor) {                            // the pressure: entity
    this->pressure_sensor_ = sensor;
  }                                                                             // end of set_pressure_sensor()
  void set_temperature_sensor(sensor::Sensor *sensor) {                         // the temperature: entity
    this->temperature_sensor_ = sensor;
  }                                                                             // end of set_temperature_sensor()
  void set_raw_pressure_sensor(sensor::Sensor *sensor) {                        // the raw_pressure: entity (diagnostic)
    this->raw_pressure_sensor_ = sensor;
  }
  void set_status_text_sensor(text_sensor::TextSensor *sensor) {                // the status: entity (diagnostic)
    this->status_text_sensor_ = sensor;
  }
  void set_pressure_range(float min, float max) {                               // min_pressure / max_pressure
    this->pressure_min_ = min;
    this->pressure_max_ = max;
  }                                                                             // end of set_pressure_range()
  void set_valid_count_range(uint16_t min, uint16_t max) {                      // min_valid_counts / max_valid_counts
    this->min_valid_counts_ = min;
    this->max_valid_counts_ = max;
  }
  void set_count_range(uint16_t zero, uint16_t span) {                          // zero_counts / span_counts
    this->zero_counts_ = zero;
    this->span_counts_ = span;
  }                                                                             // end of set_count_range()
  void set_sleep_mode(bool sleep_mode) { this->sleep_mode_ = sleep_mode; }      // sleep_mode
  void set_measurement_delay(uint32_t ms) { this->measurement_delay_ms_ = ms; } // measurement_delay
  void set_max_failures(uint8_t n) { this->max_failures_ = n; }                 // max_failures (0 = never publish NAN)

 protected:                                                                     // internal - only used inside m3200.cpp
  void read_data_();                                                            // fetch the 4-byte packet, decode it and publish
  void comm_failed_(const char *what);                                          // log a bus failure once per outage + set the warning
  void count_failure_();                                                        // count a read without data; publish NAN at max_failures
  void set_status_(const char *status);                                         // publish the status: entity when it changes
  void publish_nan_();                                                          // publish NAN ("unknown") on both entities

  sensor::Sensor *pressure_sensor_{nullptr};                                    // nullptr = no pressure: block in the YAML
  sensor::Sensor *temperature_sensor_{nullptr};                                 // nullptr = no temperature: block in the YAML
  sensor::Sensor *raw_pressure_sensor_{nullptr};                                // nullptr = no raw_pressure: block in the YAML
  text_sensor::TextSensor *status_text_sensor_{nullptr};                        // nullptr = no status: block in the YAML
  const char *last_status_{nullptr};                                            // last text published on status_text_sensor_
  float pressure_min_{0.0f};                                                    // pressure at zero_counts_ (bottom of the range)
  float pressure_max_{100.0f};                                                  // pressure at span_counts_ (top of the range)
  uint16_t zero_counts_{1000};                                                  // count at min pressure (datasheet p3: typical 1000)
  uint16_t min_valid_counts_{250};                                              // counts below this are discarded as implausible
  uint16_t max_valid_counts_{15750};                                            // ...and above this
  uint16_t span_counts_{15000};                                                 // count at max pressure (datasheet p3: typical 15000)
  bool sleep_mode_{true};                                                       // true = send a measurement request before every read (safe for both part types)
  uint32_t measurement_delay_ms_{15};                                           // wait between the request and the fetch (8.4 ms max per datasheet p3, + margin)
  uint8_t stale_count_{0};                                                      // stale packets in a row (reset by a fresh one)
  uint8_t max_failures_{3};                                                     // reads in a row without data before publishing NAN
  uint8_t failure_count_{0};                                                    // reads in a row without data (reset by a good one)
  bool out_of_range_{false};                                                    // the last packet's count was outside the valid window
  bool stale_retried_{false};                                                   // sleep mode: already retried this conversion's fetch
  bool comm_ok_{true};                                                          // false = the last bus transfer failed (no answer since)
};                                                                              // end of class M3200Component

}  // namespace m3200
}  // namespace esphome

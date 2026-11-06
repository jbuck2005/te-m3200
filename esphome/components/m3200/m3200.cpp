/*    m3200.cpp - ESPHome sensor component for the TE Connectivity M3200 (see m3200.h)
 *
 *    Page numbers refer to docs/ENG_DS_M3200_A21.pdf (datasheet, rev A21; pages match A20) and docs/Interfacing_to_DigitalPressure_Modules_A3.pdf (I2C guide).
 */
#include "m3200.h"                                                              // our class definition
#include "m3200_decode.h"                                                       // packet decoding + conversions shared with the Arduino library
#include "esphome/core/log.h"                                                   // ESP_LOGx() logging macros

namespace esphome {
namespace m3200 {

static const char *const TAG = "m3200";                                         // tag on every log line from this component, e.g. [W][m3200:042]

static const uint32_t STALE_RETRY_MS = 5;                                       // sleep mode: wait before re-fetching a stale packet once
static const uint8_t STALE_WARNING_COUNT = 5;                                   // this many stale packets in a row = values are frozen, warn

/*********************************************************************************************************************
 * SETUP
 ********************************************************************************************************************/
void M3200Component::setup() {                                                  // runs once at boot, after the I2C bus is set up
  uint8_t data[4];                                                              // throw-away buffer for the dummy read
  if (this->read(data, sizeof(data)) != i2c::ERROR_OK) {                        // dummy read: proves the sensor answers + clears the stale first packet
    // Not mark_failed(): that would stop update() until the next reboot, so a cable plugged in late, a slow
    // supply or a brown-out would leave the sensor dead. update() keeps trying and logs when it starts answering.
    ESP_LOGE(TAG, "Communication with M3200 failed at boot - will keep trying every update");
    this->comm_ok_ = false;                                                     // remembered so the recovery is logged once
    this->status_set_warning();                                                 // flags the component as "warning" until it answers
    return;                                                                     // nothing more to check
  }                                                                             // end of answer check
  switch (m3200_decode::decode(data).status) {                                  // the sensor answered: check its status bits too
    case M3200_STATUS_FAULT:                                                    // 0b11 - latched until power-on reset, so say so at boot
      ESP_LOGE(TAG, "Sensor reports a fault at boot; it latches until the sensor is power cycled");
      this->status_set_warning();                                               // flags the component as "warning" from the start
      break;
    case M3200_STATUS_RESERVED:                                                 // 0b01 - should never happen
      ESP_LOGW(TAG, "Sensor returned reserved status bits at boot");
      break;
    default:                                                                    // ok or stale: normal after power-up
      break;
  }                                                                             // end of status check
}                                                                               // end of setup()

/*********************************************************************************************************************
 * DUMP_CONFIG
 ********************************************************************************************************************/
void M3200Component::dump_config() {                                            // prints the settings to the log once at boot
  ESP_LOGCONFIG(TAG, "M3200:");                                                 // heading
  LOG_I2C_DEVICE(this);                                                         // bus + address
  if (!this->comm_ok_) {                                                        // setup() could not reach the sensor
    ESP_LOGE(TAG, "  Communication with M3200 failed at boot (retrying every update)");  // repeat it where people look
  }                                                                             // end of failed check
  ESP_LOGCONFIG(TAG, "  Pressure range: %.2f .. %.2f",                          // min_pressure .. max_pressure
                this->pressure_min_, this->pressure_max_);
  ESP_LOGCONFIG(TAG, "  Count range: %u .. %u",                                 // zero_counts .. span_counts
                this->zero_counts_, this->span_counts_);
  ESP_LOGCONFIG(TAG, "  Valid counts: %u .. %u",                                // min_valid_counts .. max_valid_counts
                this->min_valid_counts_, this->max_valid_counts_);
  ESP_LOGCONFIG(TAG, "  Sleep mode: %s", YESNO(this->sleep_mode_));             // YESNO() turns true/false into "YES"/"NO"
  if (this->sleep_mode_)                                                        // the wait only applies in sleep mode
    ESP_LOGCONFIG(TAG, "  Measurement delay: %u ms", (unsigned) this->measurement_delay_ms_);
  if (this->max_failures_ == 0)
    ESP_LOGCONFIG(TAG, "  Max failures: never publish unknown");
  else
    ESP_LOGCONFIG(TAG, "  Max failures: %u (then publish unknown)", (unsigned) this->max_failures_);
  LOG_UPDATE_INTERVAL(this);                                                    // update_interval
  LOG_SENSOR("  ", "Pressure", this->pressure_sensor_);                         // name, unit, filters ... of the pressure entity
  LOG_SENSOR("  ", "Temperature", this->temperature_sensor_);                   // ...and of the temperature entity
  LOG_SENSOR("  ", "Raw pressure", this->raw_pressure_sensor_);                 // diagnostic entities, if configured
  LOG_TEXT_SENSOR("  ", "Status", this->status_text_sensor_);
}                                                                               // end of dump_config()

/*********************************************************************************************************************
 * UPDATE
 ********************************************************************************************************************/
void M3200Component::update() {                                                 // runs every update_interval (1s in full_example.yaml)
  if (!this->sleep_mode_) {                                                     // non-sleep part: it measures continuously on its own
    this->read_data_();                                                         // so just read the latest result
    return;                                                                     // done
  }                                                                             // end of non-sleep path
  // Measurement request: the sensor's address with no data wakes it + starts one measurement.
  // The I2C guide (p4, figure 1.6 example 1) shows it as a Read_MR (READ bit); a zero-byte write sends the same thing
  // with the WRITE bit (a "Write_MR"), which the sensor also accepts. Zero-length writes depend on the ESP-IDF I2C
  // driver, so after an ESPHome upgrade check the log for "Measurement request failed".
  if (this->write(nullptr, 0) != i2c::ERROR_OK) {                               // zero-byte write = {S} address [A] {S}
    this->comm_failed_("Measurement request failed");                           // sensor did not ACK - log it once, set the warning
    return;                                                                     // try again next update
  }                                                                             // end of request check
  this->stale_retried_ = false;                                                 // allow one quick retry if this conversion isn't ready in time
  this->set_timeout("fetch", this->measurement_delay_ms_,                       // come back later (15 ms default) without blocking the rest of ESPHome
                    [this]() { this->read_data_(); });                          // ...and read the result then
}                                                                               // end of update()

/*********************************************************************************************************************
 * COMM_FAILED_
 ********************************************************************************************************************/
void M3200Component::comm_failed_(const char *what) {                          // the sensor didn't answer
  if (this->comm_ok_) {                                                         // first failure in a row: warn once
    ESP_LOGW(TAG, "%s - check wiring, pull-ups and address (logged once until it answers again)", what);
  } else {                                                                      // still failing: keep the log quiet
    ESP_LOGV(TAG, "%s", what);
  }
  this->comm_ok_ = false;                                                       // the next good read logs the recovery
  this->set_status_("no answer");
  this->status_set_warning();                                                   // flags the component as "warning" in the log
  this->count_failure_();                                                       // after max_failures in a row, values go to "unknown"
}                                                                               // end of comm_failed_()

/*********************************************************************************************************************
 * COUNT_FAILURE_ / PUBLISH_NAN_
 ********************************************************************************************************************/
void M3200Component::count_failure_() {                                         // one more read in a row with no usable data
  if (this->failure_count_ < 255)                                               // stop at 255 so it can't wrap to 0
    this->failure_count_++;
  if (this->max_failures_ != 0 && this->failure_count_ == this->max_failures_) {  // reached the limit (0 = never): say it once
    ESP_LOGW(TAG, "%u reads in a row without data - publishing unknown instead of the last value",
             (unsigned) this->max_failures_);
    this->publish_nan_();                                                       // so Home Assistant / MQTT stop showing a frozen value
  }
}                                                                               // end of count_failure_()

void M3200Component::set_status_(const char *status) {                          // publish only on change, to keep HA history small
  if (this->status_text_sensor_ == nullptr || status == this->last_status_)     // no status: entity, or nothing new
    return;
  this->last_status_ = status;                                                  // pointers to string literals, so == compares them
  this->status_text_sensor_->publish_state(status);
}                                                                               // end of set_status_()

void M3200Component::publish_nan_() {                                           // NAN shows as "unknown" in Home Assistant
  if (this->pressure_sensor_ != nullptr)                                        // only if pressure: is in the YAML
    this->pressure_sensor_->publish_state(NAN);
  if (this->temperature_sensor_ != nullptr)                                     // only if temperature: is in the YAML
    this->temperature_sensor_->publish_state(NAN);
}                                                                               // end of publish_nan_()

/*********************************************************************************************************************
 * READ_DATA_
 ********************************************************************************************************************/
void M3200Component::read_data_() {                                             // Read_DF4 (I2C guide p4, figure 1.6 example 4)
  uint8_t data[4];                                                              // [0] = status + pressure high, [1] = pressure low, [2] = temp high, [3] = temp low
  if (this->read(data, sizeof(data)) != i2c::ERROR_OK) {                        // read 4 bytes from the sensor
    this->comm_failed_("Reading data failed");                                  // no answer / short read - log it once, set the warning
    return;                                                                     // publish nothing yet - see count_failure_()
  }                                                                             // end of read check
  if (!this->comm_ok_) {                                                        // first answer after a communication failure
    ESP_LOGI(TAG, "M3200 is answering again");                                  // say so once
    this->comm_ok_ = true;
  }                                                                             // end of recovery check

  /*
   * data[0] = SSHHHHHH    S = status bits [15:14]     H = pressure bits [13:8]
   * data[1] = LLLLLLLL    L = pressure bits [7:0]
   * data[2] = TTTTTTTT    T = temperature bits [10:3]
   * data[3] = tttxxxxx    t = temperature bits [2:0]  x = junk
   */
  m3200_decode::Packet packet = m3200_decode::decode(data);                     // split into status + 14-bit pressure + 11-bit temperature
  auto status = static_cast<M3200Status>(packet.status);                        // 0..3, same values as M3200Status
  switch (status) {                                                             // decide what to do with this packet (I2C guide p5, table 1)
    case M3200_STATUS_OK:                                                       // 0b00 fresh data
      this->stale_count_ = 0;                                                   // the sensor is measuring - restart the stale count
      break;                                                                    // carry on and publish
    case M3200_STATUS_STALE:                                                    // 0b10 same values as the previous fetch - still valid, just not new
      if (this->sleep_mode_ && !this->stale_retried_) {                         // sleep mode: we just asked for a conversion, so it may be late
        this->stale_retried_ = true;                                            // retry once only
        ESP_LOGV(TAG, "Stale right after a measurement request - retrying the fetch once");
        this->set_timeout("fetch", STALE_RETRY_MS, [this]() { this->read_data_(); });
        return;                                                                 // don't count or publish this one
      }
      if (this->stale_count_ < 255)                                             // count stale packets in a row (stop at 255 so it can't wrap to 0)
        this->stale_count_++;
      if (this->stale_count_ < STALE_WARNING_COUNT) {                           // one or two now and then: just polling a bit fast
        ESP_LOGD(TAG, "Stale data (polling faster than the sensor's update rate?)");
      } else {                                                                  // the sensor has stopped measuring: values are frozen
        if (this->stale_count_ == STALE_WARNING_COUNT) {                        // say it once, not on every update
          ESP_LOGW(TAG, "%u stale readings in a row - values are frozen, publishing unknown until fresh data arrives. "
                        "Check sleep_mode matches the part number's sleep digit (...-000105-... = sleep_mode: true)",
                   STALE_WARNING_COUNT);
          this->publish_nan_();                                                 // stop frozen values flowing into HA's long-term statistics
          this->set_status_("frozen");
        }
        this->status_set_warning();                                             // flags the component as "warning" until fresh data arrives
        return;                                                                 // don't republish frozen values
      }                                                                         // end of stale count check
      break;                                                                    // a few stale packets: still valid, publish
    case M3200_STATUS_FAULT:                                                    // 0b11 sensor fault - latched until power-on reset
      ESP_LOGE(TAG, "Sensor reports a fault; it latches until power-on reset");
      this->status_set_warning();                                               // flags the component as "warning" in the log
      this->publish_nan_();                                                     // a fault is never a valid reading - "unknown" straight away
      this->set_status_("fault");
      if (this->failure_count_ < 255)                                           // still counts as a read without data
        this->failure_count_++;
      return;                                                                   // nothing valid to decode
    default:                                                                    // 0b01 reserved
      ESP_LOGW(TAG, "Reserved status bits set, discarding reading");
      this->set_status_("reserved");
      this->status_set_warning();                                               // flags the component as "warning" in the log
      this->count_failure_();                                                   // after max_failures in a row, values go to "unknown"
      return;                                                                   // nothing valid to decode
  }                                                                             // end of status switch

  uint16_t raw_pressure = packet.raw_pressure;                                  // 0..16383
  uint16_t raw_temperature = packet.raw_temperature;                            // 0..2047

  if (!m3200_decode::in_window(raw_pressure, this->min_valid_counts_, this->max_valid_counts_)) {  // plausibility window
    // A count like 0 or 16383 is almost always wiring, supply or over-range, not a real pressure: don't publish it.
    if (this->raw_pressure_sensor_ != nullptr)                                  // the raw count is still useful for diagnosis
      this->raw_pressure_sensor_->publish_state(raw_pressure);
    if (!this->out_of_range_) {                                                 // first one in a row: log + publish unknown once
      ESP_LOGW(TAG, "Pressure count %u is outside the valid range %u..%u - discarding readings until it's back",
               raw_pressure, this->min_valid_counts_, this->max_valid_counts_);
      this->publish_nan_();                                                     // never show an implausible value as a pressure
      this->out_of_range_ = true;
    }
    this->set_status_("out of range");
    this->status_set_warning();
    if (this->failure_count_ < 255)                                             // counts as a read without data
      this->failure_count_++;
    return;
  }

  float pressure = m3200_decode::counts_to_pressure(                            // datasheet p6 (formula in m3200_decode.h)
      raw_pressure, this->zero_counts_, this->span_counts_, this->pressure_min_, this->pressure_max_);
  float temperature = m3200_decode::counts_to_temperature(raw_temperature);     // datasheet p7: 11 bits cover -50 .. +150 C

  ESP_LOGV(TAG, "Raw pressure=%u temperature=%u -> %.2f, %.1f°C",               // only shown with logger: level: VERBOSE
           raw_pressure, raw_temperature, pressure, temperature);

  this->set_status_(status == M3200_STATUS_OK ? "ok" : "stale");                // the status: entity, if configured
  if (this->raw_pressure_sensor_ != nullptr)                                    // only if raw_pressure: is in the YAML
    this->raw_pressure_sensor_->publish_state(raw_pressure);                    // the count before conversion, for spotting bad data
  if (this->pressure_sensor_ != nullptr)                                        // only if pressure: is in the YAML
    this->pressure_sensor_->publish_state(pressure);                            // runs the YAML filters:, then sends to HA / MQTT
  if (this->temperature_sensor_ != nullptr)                                     // only if temperature: is in the YAML
    this->temperature_sensor_->publish_state(temperature);                      // ...same for temperature
  this->failure_count_ = 0;                                                     // usable data again
  this->out_of_range_ = false;                                                  // ...and plausible
  if (this->stale_count_ < STALE_WARNING_COUNT)                                 // frozen values keep the warning up
    this->status_clear_warning();                                               // a good reading clears any earlier warning
}                                                                               // end of read_data_()

}  // namespace m3200
}  // namespace esphome

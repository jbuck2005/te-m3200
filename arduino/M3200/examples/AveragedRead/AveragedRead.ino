/*    AveragedRead - non-blocking averaging over a running window of samples
 *
 *    Initial coding: 20261101        jbuck2005
 *
 *    Takes a sample every 50 ms and prints the average of the last 10 good samples (about twice a second).
 *    Stale and faulted packets are skipped so they never drag the average around.
 *
 *    Written for a sleep-mode part: each fetch is followed by a measurement request, so the next fetch
 *    (50 ms later, well past the 8.4 ms conversion time) sees fresh data. Harmless on non-sleep parts.
 *
 *    "Non-blocking" means loop() never sits in delay(): it checks the clock and returns straight away
 *    if it is not time for a sample yet, so anything else you add to loop() keeps running.
 */

/*********************************************************************************************************************
 * LIBRARIES
 ********************************************************************************************************************/
#include <Wire.h>                                                               // Arduino I2C library
#include <M3200.h>                                                              // this library

/*********************************************************************************************************************
 * CONFIGURATION
 ********************************************************************************************************************/
#define SDA_PIN 21                                                              // I2C data pin (ESP32 default)
#define SCL_PIN 22                                                              // I2C clock pin (ESP32 default)

const uint8_t NUM_SAMPLES = 10;                                                 // number of good samples in each average
const uint32_t SAMPLE_INTERVAL_MS = 50;                                         // time between samples in milliseconds

M3200 sensor(100);                                                              // 0..100 psi part - change 100 to your part's range

/*********************************************************************************************************************
 * PROGRAM VARIABLES
 ********************************************************************************************************************/
float pressureSum = 0;                                                          // running total of pressure samples
float temperatureSum = 0;                                                       // running total of temperature samples
uint8_t sampleCount = 0;                                                        // good samples collected so far for this average
uint32_t lastSample = 0;                                                        // millis() when the last sample was taken
M3200Status lastReported = M3200_OK;                                            // last problem printed, so each one is only printed once

/*********************************************************************************************************************
 * SETUP
 ********************************************************************************************************************/
void setup() {                                                                  // runs once at power-up
  Serial.begin(115200);                                                         // start the serial port for printing readings
#if defined(ESP32) || defined(ESP8266)                                          // these boards let you choose the I2C pins
  Wire.begin(SDA_PIN, SCL_PIN);                                                 // start I2C on the pins defined above
#else                                                                           // Uno, Nano, Mega ... have fixed I2C pins
  Wire.begin();                                                                 // start I2C on the board's default pins
#endif                                                                          // end of board check
  if (!sensor.begin()) {                                                        // try talking to the sensor
    Serial.print("M3200 not ready: ");                                          // label
    Serial.println(M3200::statusToString(sensor.lastStatus()));                 // "i2c error" = check wiring/address, "fault" = power cycle
  }                                                                             // end of sensor check
  sensor.requestMeasurement();                                                  // start the first conversion so the first fetch has fresh data
}                                                                               // end of setup()

/*********************************************************************************************************************
 * MAIN PROGRAM LOOP
 ********************************************************************************************************************/
void loop() {                                                                   // runs over and over, forever
  if (millis() - lastSample < SAMPLE_INTERVAL_MS) return;                       // not time yet - leave (the subtraction still works when millis() rolls over)
  lastSample = millis();                                                        // remember when this sample was taken

  M3200Status status = sensor.fetch();                                          // read the conversion started last time (no waiting)
  sensor.requestMeasurement();                                                  // start the next conversion right away (harmless on non-sleep parts)
  if (status != M3200_OK && status != M3200_STALE) {                            // fault, reserved or bus error: nothing useful to average
    if (status != lastReported) {                                               // print each new problem once, not 20 times a second
      Serial.print("Read failed: ");                                            // label
      Serial.print(M3200::statusToString(status));                              // "fault", "reserved" or "i2c error"
      if (status == M3200_FAULT) Serial.print(" - needs power cycle");          // the fault is latched until power is removed
      Serial.println();                                                         // new line
      lastReported = status;                                                    // remember it so it isn't printed again
    }                                                                           // end of new-problem check
    return;                                                                     // skip this sample
  }                                                                             // end of error check
  if (status == M3200_STALE) return;                                            // not a new measurement: skip it so it isn't averaged twice
  if (lastReported != M3200_OK) {                                               // a problem was printed earlier and has now cleared
    Serial.println("Readings back to normal");                                  // say so once
    lastReported = M3200_OK;                                                    // ...and start watching for problems again
  }                                                                             // end of recovery check

  pressureSum += sensor.pressure();                                             // add this sample to the running totals
  temperatureSum += sensor.temperature();                                       // ...
  if (++sampleCount < NUM_SAMPLES) return;                                      // count it, and keep collecting until we have NUM_SAMPLES

  Serial.print("Avg pressure: ");                                               // label
  Serial.print(pressureSum / sampleCount, 2);                                   // average = total / number of samples, 2 decimal places
  Serial.print(" psi\tAvg temp: ");                                             // \t = tab
  Serial.print(temperatureSum / sampleCount, 1);                                // average temperature, 1 decimal place
  Serial.println(" C");                                                         // unit + new line
  pressureSum = temperatureSum = 0;                                             // reset the totals for the next average
  sampleCount = 0;                                                              // ...and the sample counter
}                                                                               // end of loop()

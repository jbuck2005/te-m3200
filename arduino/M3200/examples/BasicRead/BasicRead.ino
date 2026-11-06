/*    BasicRead - read pressure and temperature from an M3200 once a second
 *
 *    Initial coding: 20261101        jbuck2005
 *
 *    Part-number     : M32JM-000105-100PG (0..100 psi gauge, sleep mode, address 0x28)
 *
 *    Wiring:
 *      Red   = 3V3
 *      Black = GND
 *      Green = SDA   (use 4k7 pull-up)   GPIO21 (ESP32 default)
 *      White = SCL   (use 4k7 pull-up)   GPIO22
 *
 *    Open the Serial Monitor at 115200 baud to see the readings.
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

M3200 sensor(100);                                                              // M32JM-000105-100PG: 0..100 psi - change 100 to your part's range

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
  // sensor.setSleepMode(false);                                                // sleep mode is on by default; optional for non-sleep parts (sleep digit = 0)
  if (!sensor.begin()) {                                                        // try talking to the sensor
    Serial.print("M3200 not ready: ");                                          // label
    Serial.println(M3200::statusToString(sensor.lastStatus()));                 // "i2c error" = check wiring/address, "fault" = power cycle
  }                                                                             // end of sensor check
}                                                                               // end of setup()

/*********************************************************************************************************************
 * MAIN PROGRAM LOOP
 ********************************************************************************************************************/
void loop() {                                                                   // runs over and over, forever
  M3200Status status = sensor.read();                                           // request + fetch one reading (takes ~15 ms in sleep mode)
  if (status == M3200_OK || status == M3200_STALE) {                            // both carry valid values (stale = same as last time)
    Serial.print("Pressure: ");                                                 // label
    Serial.print(sensor.pressure(), 1);                                         // pressure with 1 decimal place
    Serial.print(" psi\tTemp: ");                                               // \t = tab
    Serial.print(sensor.temperature(), 1);                                      // temperature with 1 decimal place
    Serial.print(" C\t(");                                                      // unit + open bracket
    Serial.print(M3200::statusToString(status));                                // "ok" or "stale"
    Serial.println(")");                                                        // close bracket + new line
    if (sensor.staleCount() == M3200_STALE_WARNING_COUNT) {                     // the sensor has stopped measuring - say it once
      Serial.println("Values are frozen - check setSleepMode() matches the part number's sleep digit");
    }                                                                           // end of frozen check
  } else {                                                                      // fault, reserved or bus error
    Serial.print("Read failed: ");                                              // label
    Serial.println(M3200::statusToString(status));                              // say what went wrong
  }                                                                             // end of status check
  delay(1000);                                                                  // wait one second before the next reading
}                                                                               // end of loop()

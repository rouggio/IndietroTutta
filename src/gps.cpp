#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "board_pins.h"
#include "gps_mock.h"

HardwareSerial GPSSerial(2);

#define GPS_RX PIN_GPS_RX
#define GPS_TX PIN_GPS_TX
#define GPS_BAUD 9600

void gpsInit()
{
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
}

void gpsLoop(TinyGPSPlus &gps)
{
    // Mock mode owns the fix exclusively: drain (don't parse) the UART so a
    // flaky real fix can never blend with the script. Buffer is still drained
    // to avoid overflow/framing rot while mocked.
    if (gpsMockActive()) {
        while (GPSSerial.available()) {
            (void)GPSSerial.read();
        }
        return;
    }
    while (GPSSerial.available())
    {
        char c = GPSSerial.read();
        gps.encode(c);
    }
}
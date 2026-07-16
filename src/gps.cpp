#include <Arduino.h>
#include <TinyGPSPlus.h>
#include "gps_debug.h"

HardwareSerial GPSSerial(2);

#define GPS_RX 16
#define GPS_TX 17
#define GPS_BAUD 9600

void gpsInit()
{
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
}

void gpsLoop(TinyGPSPlus &gps)
{
  while (GPSSerial.available())
  {
    char c = GPSSerial.read();
    gps.encode(c);
    processNMEAChar(c);
  }
}
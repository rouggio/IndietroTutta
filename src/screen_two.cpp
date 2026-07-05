#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>

#include "screen_two.h"

extern TFT_eSPI tft;

static void maybeClear(int newState)
{
    static int lastState = -1;
    if (newState != lastState)
    {
        lastState = newState;
        tft.fillScreen(TFT_BLACK);
    }
}

void drawScreenTwo(TinyGPSPlus &gps)
{
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.drawString("LAT: " + String(gps.location.lat(), 6), 10, 10, 4);
    tft.drawString("LON: " + String(gps.location.lng(), 6), 10, 40, 4);
    tft.drawString("Bearing: " + String(gps.course.deg(), 0), 10, 70, 4);
    tft.drawString("SAT: " + String(gps.satellites.value()), 10, 100, 4);
    tft.drawString("HDOP: " + String(gps.hdop.hdop(), 1), 10, 130, 4);
    tft.drawString("ALT: " + String(gps.altitude.meters(), 0) + " m", 10, 160, 4);
}
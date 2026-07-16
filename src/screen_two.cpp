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
//    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(2);
    tft.setCursor(0, 0);

    tft.printf("GPS DIAGNOSTICS\n\n");

    tft.printf("Chars : %lu\n", gps.charsProcessed());
    tft.printf("Fix   : %s\n", gps.location.isValid() ? "YES" : "NO ");
    tft.printf("Sats  : %d\n", gps.satellites.value() + "  ");

    if (gps.hdop.isValid())
        tft.printf("HDOP  : %.1f\n", gps.hdop.hdop());
    else
        tft.printf("HDOP  : --\n");

    if (gps.location.isValid())
    {
        tft.printf("Lat   : %.6f\n", gps.location.lat());
        tft.printf("Lon   : %.6f\n", gps.location.lng());
    }
    else
    {
        tft.printf("Lat   : --\n");
        tft.printf("Lon   : --\n");
    }

    if (gps.altitude.isValid())
        tft.printf("Alt   : %.1f m\n", gps.altitude.meters());

    if (gps.speed.isValid())
        tft.printf("Speed : %.1f km/h\n", gps.speed.kmph());

    tft.printf("Age   : %lu ms\n", gps.location.age());
}
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>

#include "gps_debug.h"
#include "screen_two.h"

extern TFT_eSPI tft;

void drawScreenThree(TinyGPSPlus &gps)
{
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextFont(1);
    tft.setCursor(0, 0);

    for (int i = 0; i < MAX_NMEA_LINES; i++)
    {
        int start = getNextNMEALine();
        int idx = (start + i) % MAX_NMEA_LINES;

        if (!nmeaLines[idx].isEmpty())
            tft.printf("%s\n", nmeaLines[idx].c_str());
    }
}
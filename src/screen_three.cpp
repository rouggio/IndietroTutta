#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>

#include "gps_debug.h"
#include "screen_two.h"
#include "buttons.h"

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

void screenThreeButton(
    Button button,
    ButtonEvent event
) {

    if (button == Button::Left &&
        event == ButtonEvent::ShortPress) {

        // Screen 3: left button
    }

    if (button == Button::Right &&
        event == ButtonEvent::ShortPress) {

        // Screen 3: right button
    }

    if (button == Button::Left &&
        event == ButtonEvent::LongPress) {

        // Screen 3: left long press
    }
}
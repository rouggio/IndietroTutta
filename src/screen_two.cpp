#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include "config.h"
#include "ota.h"
#include <WiFi.h>
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
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(2);
    tft.setCursor(0, 0);

    tft.printf("DIAGNOSTICS\n\n");

    tft.printf("GPS\n");
    tft.printf("Chars : %lu\n", gps.charsProcessed());

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

    if (gps.location.isValid()) {
        tft.printf("Age   : %lu ms\n", gps.location.age());
    } else {
        tft.printf("Age   :--\n");
    }


    tft.printf("\nSystem\n");
    tft.print("Version: " BUILD_VERSION "\n");
    tft.print("IP: " + WiFi.localIP().toString() + "\n");

}

void screenTwoButton(
    Button button,
    ButtonEvent event
) {

    if (button == Button::Left &&
        event == ButtonEvent::ShortPress) {

        // Screen 2: left button
    }

    if (button == Button::Right &&
        event == ButtonEvent::ShortPress) {

        // Screen 2: right button
    }

    if (button == Button::Left &&
        event == ButtonEvent::LongPress) {

        // Screen 2: left long press - check for OTA update
        checkForUpdate();
        tft.fillScreen(TFT_BLACK);
    }
}
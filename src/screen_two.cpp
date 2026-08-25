#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include "config.h"
#include <WiFi.h>
#include "screens.h"
#include "screen_two.h"

extern TFT_eSPI tft;

void drawScreenTwoMain(TinyGPSPlus &gps)
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

    // Show connected SSID if available
    String ssid = WiFi.SSID();
    if (ssid.length() > 0) {
        tft.print("SSID: " + ssid + "\n");
    } else {
        tft.print("SSID: --\n");
    }

    // Hint bar
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("l Page", 8, 235, 2);
}

void drawScreenTwo(TinyGPSPlus &gps)
{
    drawScreenTwoMain(gps);
}

void screenTwoButton(
    Button button,
    ButtonEvent event
) {
    // Left short: advance to the next page. The diagnostics page
    // has no submodes; every other event is ignored.
    if (button == Button::Left && event == ButtonEvent::ShortPress) {
        nextScreen();
        return;
    }
}

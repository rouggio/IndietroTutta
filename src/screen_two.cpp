#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include "config.h"
#include "config_store.h"
#include "ota.h"
#include <WiFi.h>
#include "screens.h"
#include "screen_two.h"
#include "serial_buffer.h"

extern TFT_eSPI tft;

enum class ScreenTwoState {
    Display,
    Menu
};

static ScreenTwoState state = ScreenTwoState::Display;

static const char* menuItems[] = {
    "Check for OTA update",
    "OTA update on boot",
    "Reboot device"
};

static const int NUM_MENU_ITEMS =
    sizeof(menuItems) / sizeof(menuItems[0]);

static int menuItem = 0;

static String autoOTAMenuLabel()
{
    return String(menuItems[1]) + ": " +
           (config.otaCheckOnStart ? "ON" : "OFF");
}



static void maybeClear(int newState)
{
    static int lastState = -1;
    if (newState != lastState)
    {
        lastState = newState;
        tft.fillScreen(TFT_BLACK);
    }
}

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

}

// --------------------------------------------------
// DRAW MENU
// --------------------------------------------------

static void drawScreenTwoMenu()
{
    tft.fillScreen(TFT_BLACK);

    tft.setTextDatum(TL_DATUM);

    tft.setTextColor(
        TFT_BLUE,
        TFT_BLACK
    );

    tft.drawString(
        "MENU",
        10,
        10,
        4
    );


    // ------------------------------------------------
    // Menu items
    // ------------------------------------------------

    for (int i = 0; i < NUM_MENU_ITEMS; i++) {

        int y = 60 + i * 35;

        if (i == menuItem) {

            tft.setTextColor(
                TFT_BLACK,
                TFT_WHITE
            );

            tft.fillRect(
                5,
                y - 3,
                310,
                30,
                TFT_WHITE
            );
        }
        else {

            tft.setTextColor(
                TFT_WHITE,
                TFT_BLACK
            );
        }

        String label = (i == 1) ? autoOTAMenuLabel() : menuItems[i];

        tft.drawString(
            label,
            15,
            y,
            2
        );
    }


    // ------------------------------------------------
    // Help
    // ------------------------------------------------
    tft.setTextColor(
        TFT_DARKGREY,
        TFT_BLACK
    );

    tft.setTextDatum(BL_DATUM);
    tft.drawString(
        "L: Back",
        5,
        235,
        1
    );

    tft.setTextDatum(BR_DATUM);
    tft.drawString(
        "R: Select, Long R: Execute",
        315,
        235,
        1
    );
}

// --------------------------------------------------
// PUBLIC DRAW FUNCTION
// --------------------------------------------------

void drawScreenTwo(TinyGPSPlus &gps)
{
    if (state == ScreenTwoState::Menu) {
        return;
    }

    if (state == ScreenTwoState::Menu) {
        drawScreenTwoMenu();
    }
    else {
        drawScreenTwoMain(gps);
    }
}

// --------------------------------------------------
// EXECUTE MENU ITEM
// --------------------------------------------------

static void executeMenuItem()
{
    switch (menuItem) {

        case 0:
            // This takes over the display while
            // checking/downloading the OTA update.
            checkForUpdate();
            break;
        case 1:
        {
            const bool previousValue = config.otaCheckOnStart;
            config.otaCheckOnStart = !previousValue;

            if (!saveConfig(config)) {
                bufferedSerialPrintln("[OTA] Failed to save auto-check preference");
                config.otaCheckOnStart = previousValue;
            }

            drawScreenTwoMenu();
            break;
        }
        case 2:
            // Reboot device
            ESP.restart();
            break;

    }
}

// --------------------------------------------------
// BUTTON HANDLER
// --------------------------------------------------

void screenTwoButton(
    Button button,
    ButtonEvent event
)
{
    // =================================================
    // NORMAL SCREEN
    // =================================================

    if (state == ScreenTwoState::Display) {

        // Left short → next screen
        if (button == Button::Left &&
            event == ButtonEvent::ShortPress) {

            // Screen 1: left button
            nextScreen();
        }
        
        // Left long → open menu
        if (button == Button::Left &&
            event == ButtonEvent::LongPress) {

            state = ScreenTwoState::Menu;

            menuItem = 0;

            drawScreenTwoMenu();
        }

        return;
    }


    // =================================================
    // MENU
    // =================================================

    // Left short → exit menu
    if (button == Button::Left &&
        event == ButtonEvent::ShortPress) {

        state = ScreenTwoState::Display;

        // The normal screen will be redrawn by
        // screenLoop().
        tft.fillScreen(TFT_BLACK);

        return;
    }


    // Right short → next menu item
    if (button == Button::Right &&
        event == ButtonEvent::ShortPress) {

        menuItem++;

        if (menuItem >= NUM_MENU_ITEMS) {
            menuItem = 0;
        }

        drawScreenTwoMenu();

        return;
    }


    // Right long → execute selected item
    if (button == Button::Right &&
        event == ButtonEvent::LongPress) {

        executeMenuItem();

        return;
    }
}

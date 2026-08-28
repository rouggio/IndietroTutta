#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>

#include "config_store.h"
#include "ota.h"
#include "screens.h"
#include "serial_buffer.h"
#include "screen_config.h"

extern TFT_eSPI tft;

// ==== COLORS ====
#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define GRAY 0x7BEF

enum ConfigRow {
    ROW_OTA = 0,
    ROW_SPEED = 1,
    ROW_COUNT
};

static int selRow = ROW_OTA;
static bool needsRedraw = true;

static const char* speedLabel()
{
    static const char* labels[SPEED_UNITS] = { "kn", "km/h", "mph" };
    return labels[config.speedUnit];
}

static void persistConfig()
{
    if (!saveConfig(config)) {
        bufferedSerialPrintln("[CONFIG] Failed to save configuration");
    }
}

void drawScreenConfig(bool requiresInit)
{
    if (requiresInit) {
        tft.fillScreen(BG);
        needsRedraw = true;
    }

    // ---- Title ----
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("CONFIG", tft.width() / 2, 20, 4);
    tft.drawFastHLine(20, 44, tft.width() - 40, GRAY);

    // ---- Selectable rows ----
    if (needsRedraw) {
        tft.fillRect(0, 60, tft.width(), 70, BG);

        for (int i = 0; i < ROW_COUNT; i++) {
            int y = 68 + i * 30;
            bool selected = (i == selRow);

            tft.setTextColor(selected ? TFT_YELLOW : WHITE, BG);
            String row = selected ? "> " : "   ";

            if (i == ROW_OTA)
                row += String("OTA on boot : ") +
                       (config.otaCheckOnStart ? "ON" : "OFF");
            else
                row += String("Speed unit  : ") + speedLabel();

            tft.setTextDatum(TL_DATUM);
            tft.drawString(row, 12, y, 2);
        }

        needsRedraw = false;
    }

    // ---- Hint bar: L/LL on the left, R/RR on the right ----
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("L Main  LL OTA", 8, 235, 2);
    tft.setTextDatum(BR_DATUM);
    tft.drawString("R Sel  RR Set", tft.width() - 8, 235, 2);
}

void screenConfigButton(Button button, ButtonEvent event)
{
    // Left short: back to MAIN
    if (button == Button::Left && event == ButtonEvent::ShortPress) {
        setCurrentPage(PageMain);
        return;
    }

    // Right short: move the selection
    if (button == Button::Right && event == ButtonEvent::ShortPress) {
        selRow = (selRow + 1) % ROW_COUNT;
        needsRedraw = true;
        return;
    }

    // Left long: run an immediate OTA check (fullscreen takeover,
    // the display is restored by checkForUpdate itself)
    if (button == Button::Left && event == ButtonEvent::LongPress) {
        bufferedSerialPrintln("[CONFIG] Manual OTA check requested");
        checkForUpdate();
        needsRedraw = true;
        return;
    }

    // Right long: apply the selected setting
    if (button == Button::Right && event == ButtonEvent::LongPress) {
        if (selRow == ROW_OTA) {
            config.otaCheckOnStart = !config.otaCheckOnStart;
            bufferedSerialPrintln(config.otaCheckOnStart ?
                "[CONFIG] OTA on boot enabled" :
                "[CONFIG] OTA on boot disabled");
        } else {
            config.speedUnit = (config.speedUnit + 1) % SPEED_UNITS;
            bufferedSerialPrintln("[CONFIG] Speed unit changed");
        }

        persistConfig();
        needsRedraw = true;
        return;
    }
}

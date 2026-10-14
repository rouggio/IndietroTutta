#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>

#include "config_store.h"
#include "ota.h"
#include "screens.h"
#include "serial_buffer.h"
#include "screen_config.h"
#include "server_link.h"
#include "sound.h"

#include "canvas.h"

// ==== COLORS ====
#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define GRAY 0x7BEF

enum ConfigRow {
    ROW_OTA = 0,
    ROW_SERVER = 1,
    ROW_SPEED = 2,
    ROW_BEEP = 3,
    ROW_BOOTSOUND = 4,
    ROW_GUNSOUND = 5,
    ROW_COUNT
};

// Six rows at 22px keep the detail line on screen (font 2 is 16px tall):
// rows occupy 68..190, the detail sits at 192..208, the hint bar at 214.
static const int ROW_Y0 = 68;
static const int ROW_DY = 22;
static const int DETAIL_Y = 192;
static const int WIPE_HEIGHT = 148;

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
    tft.drawFastHLine(0, 44, tft.width(), GRAY);

    // ---- Selectable rows ----
    if (needsRedraw) {
        // Wipe the rows AND the detail line, which sits below the last row
        // at DETAIL_Y. Stops just short of the hint bar at y=214.
        tft.fillRect(0, 60, tft.width(), WIPE_HEIGHT, BG);

        for (int i = 0; i < ROW_COUNT; i++) {
            int y = ROW_Y0 + i * ROW_DY;
            bool selected = (i == selRow);

            tft.setTextColor(selected ? TFT_YELLOW : WHITE, BG);
            String row = selected ? "> " : "   ";

            if (i == ROW_OTA)
                row += String("OTA on boot : ") +
                       (config.otaCheckOnStart ? "ON" : "OFF");
            else if (i == ROW_SERVER)
                row += String("Server      : ") +
                       (serverMode() == 1 ? "DEV" : "PROD");
            else if (i == ROW_BEEP)
                row += String("Key beep    : ") +
                       (beepOnKeys ? "ON" : "OFF");
            else if (i == ROW_BOOTSOUND)
                row += String("Boot sound  : ") +
                       (bootSoundOn ? "ON" : "OFF");
            else if (i == ROW_GUNSOUND)
                row += String("Gun cue     : ") +
                       (gunSoundOn ? "ON" : "OFF");
            else
                row += String("Speed unit  : ") + speedLabel();

            tft.setTextDatum(TL_DATUM);
            tft.drawString(row, 12, y, 2);
        }

        // Detail line under the rows: what PROD/DEV actually means right
        // now. Host is set once via portal `/server` (typing an IP with
        // two buttons is nobody's idea of fun); the toggle here flips mode.
        String detail;
        if (serverMode() == 1) {
            const String host = serverDevHost();
            detail = host.length() ? String("-> ") + host : "-> no host! use portal";
        } else {
            detail = "-> Render prod";
        }
        tft.setTextColor(GRAY, BG);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(detail, 12, DETAIL_Y, 2);

        needsRedraw = false;
    }

    // ---- Hint bar: L/LL on the left, R/RR on the right ----
    tft.drawFastHLine(0, 214, tft.width(), GRAY);
    tft.setTextColor(GRAY, BG);
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
            persistConfig();
        } else if (selRow == ROW_SERVER) {
            const int next = (serverMode() == 1) ? 0 : 1;
            serverSetMode(next);
            bufferedSerialPrintln(String("[CONFIG] Server -> ") + serverBaseUrl());
            // server keys persist in their own NVS namespace, not cfg blob
        } else if (selRow == ROW_BEEP) {
            const bool next = !beepOnKeys;
            setBeepOnKeys(next);
            bufferedSerialPrintln(next ?
                "[CONFIG] Key beep enabled" :
                "[CONFIG] Key beep disabled");
            // own NVS key too — see config_store.h
            if (next) soundButtonPress();  // audible confirmation
        } else if (selRow == ROW_BOOTSOUND) {
            const bool next = !bootSoundOn;
            setBootSound(next);
            bufferedSerialPrintln(next ?
                "[CONFIG] Boot sound enabled (plays at next power-up)" :
                "[CONFIG] Boot sound disabled (next power-up will be silent)");
            // Deliberately no audible confirmation here: enabling it must
            // not chirp now, or it would defeat the point of turning it off.
        } else if (selRow == ROW_GUNSOUND) {
            const bool next = !gunSoundOn;
            setGunSound(next);
            bufferedSerialPrintln(next ?
                "[CONFIG] Gun cue enabled" :
                "[CONFIG] Gun cue disabled");
            if (next) soundGun(false);  // audible confirmation
        } else {
            config.speedUnit = (config.speedUnit + 1) % SPEED_UNITS;
            bufferedSerialPrintln("[CONFIG] Speed unit changed");
            persistConfig();
        }

        needsRedraw = true;
        return;
    }
}

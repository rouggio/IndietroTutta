#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>

#include "config_store.h"
#include "gps_mock.h"
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
    ROW_FX = 6,   // flips the fix source real GPS <-> mock
    ROW_COUNT
};

// Seven rows at 21px (font 2 is 16px tall): rows occupy 64..206 and the hint
// bar rule sits at 214. The enum order IS the display order and every row is
// always offered — the list used to lose FX in PROD, which put "set the fix
// source" out of reach on a prod-mounted boat that wants a scripted run.
static const int ROW_Y0 = 64;
static const int ROW_DY = 21;
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
        // Entering the screen always puts the highlight back on the first
        // entry: the last row you touched is rarely the one you came back for.
        selRow = ROW_OTA;
        needsRedraw = true;
    }

    // ---- Title ----
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("CONFIG", tft.width() / 2, 20, 4);
    tft.drawFastHLine(0, 44, tft.width(), GRAY);

    // ---- Selectable rows ----
    if (needsRedraw) {
        // Wipe the whole row block in one go, stopping just short of the hint
        // bar at y=214 (60..208 covers the last row at 64 + 6*21 = 190..206).
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
            else if (i == ROW_FX)
                row += String("FX source   : ") +
                       (gpsMockActive() ? "MOCK" : "REAL");
            else
                row += String("Speed unit  : ") + speedLabel();

            tft.setTextDatum(TL_DATUM);
            tft.drawString(row, 12, y, 2);
        }

        // Detail line under the rows: what PROD/DEV actually means right
        // now. Host is set once via portal `/server` (typing an IP with
        // two buttons is nobody's idea of fun); the toggle here flips mode.
        // Detail line removed (dev base URL line) per user request.
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
            serverApplyMode(next);
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
        } else if (selRow == ROW_FX) {
            // One flag, two meanings: it is the fix SOURCE (UART vs the
            // scripted backend feed) and the top bar's FX tile colour. Same
            // switch the portal's /mock drives, so the row and the portal can
            // never disagree — gpsMockSet() updates the 5s NVS cache inline.
            const bool next = !gpsMockActive();
            gpsMockSet(next);
            bufferedSerialPrintln(next ?
                "[CONFIG] FX source -> MOCK (scripted backend feed)" :
                "[CONFIG] FX source -> REAL (GPS UART)");
        } else {
            config.speedUnit = (config.speedUnit + 1) % SPEED_UNITS;
            bufferedSerialPrintln("[CONFIG] Speed unit changed");
            persistConfig();
        }

        needsRedraw = true;
        return;
    }
}

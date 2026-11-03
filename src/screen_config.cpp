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
#include "hint_bar.h"

// ==== COLORS ====
#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define GRAY 0x7BEF

// The two SOURCE settings (which server, which fix source) sit together at the
// bottom: they are the pair you come here to change when the boat has to reach
// a different backend, so they are adjacent rather than separated by the
// preferences. The enum order IS the display order.
enum ConfigRow {
    ROW_OTA = 0,
    ROW_SPEED = 1,
    ROW_BEEP = 2,
    ROW_BOOTSOUND = 3,
    ROW_GUNSOUND = 4,
    ROW_SERVER = 5,   // PROD <-> DEV, upwards of the fix source
    ROW_FX = 6,       // flips the fix source real GPS <-> mock
    ROW_COUNT
};

// Seven rows at 30px (font 4 is 26px tall): rows occupy 62..272. The enum
// order IS the display order and every row is always offered — the list used to
// lose FX in PROD, which put "set the fix source" out of reach on a
// prod-mounted boat that wants a scripted run.
static const int ROW_Y0 = 62;
static const int ROW_DY = 30;
static const int TITLE_H = 44;

// ---- Table geometry --------------------------------------------------------
// Two columns, and the panel is 480 px wide with only the hint spine taken
// (451), so there is no reason to jam everything against the left edge: the
// label column starts just inside the "> " marker and the VALUE column is
// right-aligned against the far edge of the content area, which lines the
// values up vertically no matter how long each one is. No colons - the
// alignment says "label, value" on its own.
static const int MARK_X = 18;                     // ">" for the selected row
static const int LABEL_X = 48;
static const int VALUE_RIGHT = HINT_BAR_X - 18;   // 433: right edge of the values

static int selRow = ROW_OTA;
static bool needsRedraw = true;

// Per-row change detection: a toggle repaints that ONE row, not the page. The
// values are Strings because they change width ("km/h" vs "kn") and the wipe
// below relies on knowing the row is dirty at all.
static String rowVal[ROW_COUNT];
static int8_t rowSel[ROW_COUNT];

static const char* speedLabel()
{
    static const char* labels[SPEED_UNITS] = { "kn", "km/h", "mph" };
    return labels[config.speedUnit];
}

static const char* rowLabel(int i)
{
    switch (i) {
        case ROW_OTA:       return "OTA on boot";
        case ROW_SPEED:     return "Speed unit";
        case ROW_BEEP:      return "Key beep";
        case ROW_BOOTSOUND: return "Boot sound";
        case ROW_GUNSOUND:  return "Gun cue";
        case ROW_SERVER:    return "Server";
        case ROW_FX:        return "FX source";
        default:            return "?";
    }
}

static String rowValue(int i)
{
    switch (i) {
        case ROW_OTA:       return config.otaCheckOnStart ? "ON" : "OFF";
        case ROW_SPEED:     return speedLabel();
        case ROW_BEEP:      return beepOnKeys ? "ON" : "OFF";
        case ROW_BOOTSOUND: return bootSoundOn ? "ON" : "OFF";
        case ROW_GUNSOUND:  return gunSoundOn ? "ON" : "OFF";
        case ROW_SERVER:    return serverMode() == 1 ? "DEV" : "PROD";
        case ROW_FX:        return gpsMockActive() ? "MOCK" : "REAL";
        default:            return "?";
    }
}

// One table row. Returns early when neither its value nor its selection moved,
// so the 200 ms pass costs nothing on a settled page.
static void drawRow(int i)
{
    const int y = ROW_Y0 + i * ROW_DY;
    const String v = rowValue(i);
    const int8_t sel = (i == selRow) ? 1 : 0;
    if (rowVal[i] == v && rowSel[i] == sel) return;
    rowVal[i] = v;
    rowSel[i] = sel;

    // Wipe the row's own band (not the page): full content width so a shorter
    // value cannot leave the tail of a longer one behind.
    tft.fillRect(0, y - 3, HINT_BAR_X, ROW_DY, BG);

    if (sel) {
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(TFT_YELLOW, BG);
        tft.drawString(">", MARK_X, y, 4);
    }
    tft.setTextColor(sel ? TFT_YELLOW : WHITE, BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(rowLabel(i), LABEL_X, y, 4);
    tft.setTextDatum(TR_DATUM);   // values end on a shared right edge
    tft.drawString(v, VALUE_RIGHT, y, 4);
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
        hintBarReset();   // the page was cleared: repaint the whole spine
    }

    // ---- Title + rows ----
    if (needsRedraw) {
        // Title bar is chrome: repainted on a full pass only, not every 200ms,
        // or the navy fill flickers. It stops at the hint spine, so the column
        // keeps its own pixels - same rule as MAIN's header.
        tft.fillRect(0, 0, HINT_BAR_X, TITLE_H, brandNavy());
        tft.setTextColor(WHITE, brandNavy());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("CONFIG", HINT_BAR_X / 2, TITLE_H / 2, 4);
        tft.drawFastHLine(0, TITLE_H, HINT_BAR_X, GRAY);
        // Force every row to repaint: the page (or the OTA takeover) cleared it.
        for (int i = 0; i < ROW_COUNT; i++) { rowVal[i] = ""; rowSel[i] = -1; }
        needsRedraw = false;
    }

    // Values first: the row band is wiped as part of the row itself, so a
    // toggle costs one 451x30 fill and two strings, never a page repaint.
    for (int i = 0; i < ROW_COUNT; i++) drawRow(i);

    // ---- Hint spine ----
    // Same column as MAIN, with CONFIG's own words. 1S = back to MAIN, 1L = an
    // immediate OTA check; 2S walks the selection; 3S applies (toggles) the
    // selected row. Only button 1 has a long press, so the other two cells
    // carry a single word.
    hintBarGrid();
    hintBarCell(0, "BACK", "OTA", WHITE, HINT_LONG);
    hintBarCell(1, "SELECT", nullptr, WHITE, WHITE);
    hintBarCell(2, "TOGGLE", nullptr, WHITE, WHITE);
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
        // No page repaint: drawRow() sees the selection move on the two rows
        // involved and wipes only those.
        selRow = (selRow + 1) % ROW_COUNT;
        return;
    }

    // Left long: run an immediate OTA check (fullscreen takeover,
    // the display is restored by checkForUpdate itself)
    if (button == Button::Left && event == ButtonEvent::LongPress) {
        bufferedSerialPrintln("[CONFIG] Manual OTA check requested");
        checkForUpdate();
        needsRedraw = true;
        // The takeover painted over the whole panel, spine included: force the
        // cells to repaint rather than trusting their cache.
        hintBarReset();
        return;
    }

    // 3S: apply the selected setting (menus action on 3S, not 2L)
    if (button == Button::Third && event == ButtonEvent::ShortPress) {
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

        // The row's value changed: drawRow() repaints that row only (its band
        // wipe covers a value that got shorter, e.g. km/h -> kn).
        return;
    }
}

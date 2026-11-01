#include <Arduino.h>
#include <optional>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include "wifi_manager.h"
#include "config_store.h"
#include "backend.h"
#include "buttons.h"
#include "screens.h"
#include "gps_mock.h"
#include "race_session.h"
#include "server_link.h"

#include "screen_speed.h"

#include "canvas.h"

// ==== COLORS ====
#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define GREEN TFT_GREEN
#define CYAN TFT_CYAN
#define RED TFT_RED
#define DARK_RED 0x8000
#define GRAY 0x7BEF
#define RING_GRAY 0xC618 // brighter silver — ring, ticks and N (contrast)
#define ARC_RED RED      // no-go arc: the SAME red as the wind arrow, by identity
#define RING_BAND 12     // no-go band thickness in px, inward from the ring (r-RING_BAND..r)

const int MIN_SAT_THRESHOLD = 4; // Minimum number of satellites for a good fix

const uint16_t icon_no_signal[256] PROGMEM = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
  0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 
  0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 
  0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 
  0x0000, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
  0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
  0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 
  0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 
  0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 
  0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 
  0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

const uint16_t icon_sat[256] PROGMEM = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
  0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
  0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
  0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 
  0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 
  0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 
  0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 
  0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

const uint16_t icon_wifi[256] PROGMEM = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 
  0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 
  0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 
  0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 
  0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 
  0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 
  0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
  0x0000, 0x0000, 0x0000, 0x0000
};

const uint8_t icon_cloud[] PROGMEM = {
  0x00,0x00,
  0x00,0x00,
  0x03,0xC0,
  0x0C,0x30,
  0x18,0x18,
  0x30,0x0C,
  0x60,0x06,
  0x60,0x06,
  0xFF,0xFF,
  0xFF,0xFF,
  0x00,0x00,
  0x00,0x00,
  0x00,0x00,
  0x00,0x00,
  0x00,0x00,
  0x00,0x00
};

enum class TriState {
    Unknown,
    False,
    True
};

TriState prevWifiConnected = TriState::Unknown;
TriState prevDataConnected = TriState::Unknown;
TriState prevFix = TriState::Unknown; // legacy (fix tile now uses prevFixTile)
static int prevFixTile = -1; // 0 nofix, 1 fix, 2 mock; -1 forces repaint
TriState prevDevMode = TriState::Unknown; // server-mode tile: DEV only in dev

// ====== LAYOUT ======
void drawTopBar(TinyGPSPlus &gps)
{
  // left-aligned symbols

  int tile_width = 24;
  int tile_height = 24;
  int tile_spacing = 3;
  int icon_width = 16;
  int icon_height = 16;
  int icont_offset_x = (tile_width - icon_width) / 2;

  // WiFi icon (top-left)
  int icon_x = tile_spacing;
  if (wifiConnected() && prevWifiConnected != TriState::True)
  {
    tft.fillRoundRect(icon_x, 2, tile_width, tile_height, 4, TFT_DARKGREEN);
    tft.drawRoundRect(icon_x, 2, tile_width, tile_height, 4, GREEN);
    tft.setTextColor(GREEN, TFT_DARKGREEN);
    canvasPushImage(icon_x + icont_offset_x, 5, icon_width, icon_height, icon_wifi, TFT_BLACK);
    prevWifiConnected = TriState::True;
  }
  else if (!wifiConnected() && prevWifiConnected != TriState::False)
  {
    tft.fillRoundRect(icon_x, 2, tile_width, tile_height, 4, DARK_RED);
    tft.drawRoundRect(icon_x, 2, tile_width, tile_height, 4, TFT_RED);
    tft.setTextColor(TFT_RED, DARK_RED);
    canvasPushImage(icon_x + icont_offset_x, 5, icon_width, icon_height, icon_no_signal, TFT_BLACK);
    prevWifiConnected = TriState::False;
  }

  // data connection
  icon_x += tile_width + tile_spacing;
  if (backendOnline() && prevDataConnected != TriState::True) {
    tft.fillRoundRect(icon_x, 2, tile_width, tile_height, 4, TFT_DARKGREEN);
    tft.drawRoundRect(icon_x, 2, tile_width, tile_height, 4, GREEN);
    tft.drawBitmap(icon_x + icont_offset_x, 7, icon_cloud, icon_width, icon_height, WHITE);
    prevDataConnected = TriState::True;
  } else if (!backendOnline() && prevDataConnected != TriState::False) {
    tft.fillRoundRect(icon_x, 2, tile_width, tile_height, 4, DARK_RED);
    tft.drawRoundRect(icon_x, 2, tile_width, tile_height, 4, TFT_RED);
    tft.drawBitmap(icon_x + icont_offset_x, 7, icon_cloud, icon_width, icon_height, WHITE);
    prevDataConnected = TriState::False;
  }

  // Fix Icon (yellow FX tile while the mock drives the fix instead of GPS)
  icon_x += tile_width + tile_spacing;
  const int fixTile = gpsMockActive() ? 2 : (gps.location.isValid() ? 1 : 0);
  if (fixTile != prevFixTile)
  {
    prevFixTile = fixTile;
    if (fixTile == 2)
    {
      tft.fillRoundRect(icon_x, 2, tile_width, tile_height, 4, TFT_OLIVE);
      tft.drawRoundRect(icon_x, 2, tile_width, tile_height, 4, TFT_YELLOW);
      tft.setTextColor(WHITE, TFT_OLIVE);
      tft.setTextDatum(TL_DATUM);
      tft.drawString("FX", icon_x + icont_offset_x, 7, 2);
    }
    else if (fixTile == 1)
    {
      tft.fillRoundRect(icon_x, 2, tile_width, tile_height, 4, TFT_DARKGREEN);
      tft.drawRoundRect(icon_x, 2, tile_width, tile_height, 4, GREEN);
      tft.setTextColor(WHITE, TFT_DARKGREEN);
      tft.setTextDatum(TL_DATUM);
      tft.drawString("FX", icon_x + icont_offset_x, 7, 2);
    }
    else
    {
      tft.fillRoundRect(icon_x, 2, tile_width, tile_height, 4, DARK_RED);
      tft.drawRoundRect(icon_x, 2, tile_width, tile_height, 4, TFT_RED);
      tft.setTextColor(WHITE, DARK_RED);
      tft.setTextDatum(TL_DATUM);
      tft.drawString("FX", icon_x + icont_offset_x, 6, 2);
    }
  }


  // DEV tile (server mode from CONFIG): painted only when serverMode()==1,
  // cleared when leaving dev mode. Wider than the icon tiles so font-2 "DEV"
  // fits; state-gated like the other tiles, no per-pass clear.
  icon_x += tile_width + tile_spacing;
  const int dev_w = 40;
  const bool devMode = serverMode() == 1;
  if (devMode && prevDevMode != TriState::True)
  {
    tft.fillRoundRect(icon_x, 2, dev_w, tile_height, 4, TFT_DARKCYAN);
    tft.drawRoundRect(icon_x, 2, dev_w, tile_height, 4, CYAN);
    tft.setTextColor(WHITE, TFT_DARKCYAN);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("DEV", icon_x + dev_w / 2, 7, 2);
    prevDevMode = TriState::True;
  }
  else if (!devMode && prevDevMode != TriState::False)
  {
    tft.fillRect(icon_x, 2, dev_w, tile_height, BG);
    prevDevMode = TriState::False;
  }


  // Right aligned symbols

  int sats = gps.satellites.isValid() ? gps.satellites.value() : 0;

  // GPS satellites (top-right)
  canvasPushImage(tft.width() - 57, 7, 16, 16, icon_sat, TFT_BLACK);

  tft.setTextColor(sats > 0 ? sats > MIN_SAT_THRESHOLD ? GREEN : TFT_YELLOW : TFT_RED, BG);
  tft.setTextDatum(TR_DATUM);
  String satStr = (sats < 10) ? "0" + String(sats) : String(sats);
  tft.drawString(satStr, tft.width() - 10, 5, 4);


  tft.drawFastHLine(0, 30, tft.width(), GRAY);
}

// Cell caches: values redrawn only on change (no flicker), space-padded
// to overwrite narrower predecessors. Reset in initScreen().
static String lastMaxStr = "";
static String lastSesStr = "";
static String lastWndStr = "";
static String lastBrgStr = "";
static String lastCenStr = "";
// Compass cell: angles of the triangles last drawn (mode-relative: in
// bearing-up view the boat is 0 and the wind is bearing-relative).
// -1 = nothing drawn, -2 = cell not rendered yet (sentinel — forces
// ring+N+arrows on first pass). prevRingUp/prevNDeg track the view mode
// and the angle where the N glyph was last painted (stale-N clear).
static int prevBoatDeg = -2;
static int prevWindDeg = -2;
static int prevArcDeg = -1;  // no-go arc center last drawn (-1 = none)
static int prevNoGoHalf = -1; // no-go half-width last drawn (-1 = none)
static int prevRingUp = -1;
static int prevNDeg = -1;
// Ring view: true = bearing-up (boat triangle points to the top, ring
// rotates), false = N-up (compass). R short toggles; RAM only.
static bool ringBrgUp = false;
// Instant speed: last string + font (width changes get a one-off wipe).
static String lastSpdStr = "";
static uint8_t lastSpdFont = 8;

static void drawCellLabel(const char* label, int cx, int y){
  tft.setTextColor(GRAY, BG);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(label, cx, y, 2);
}

// Largest digit font that still fits the cell. Font 6 (48 px, digits + ":-.")
// is the big one; font 4 (26 px) is the fallback for a long value.
// Measured on the TRIMMED string: the surrounding spaces exist so a redraw
// erases the tail of a shorter value, but they are not ink, and a 7-segment
// space glyph is wide enough to knock a value down a font size on its own.
static uint8_t fitValueFont(const String& s, int maxWidth) {
  String t = s;
  t.trim();
  if (!t.length()) return 6;
  return tft.textWidth(t, 6) <= maxWidth ? 6 : 4;
}

// ====== LAYOUT (measured in mm, converted at 6.49 px/mm) ======
// Panel 74 x 52 mm, 480 x 320 px at rotation 1 -> 480/74 = 6.49 px/mm.
// (Pixels are square, so 52 mm would be 337 px: the module carries a little
// bezel; the width is the honest reference, usable height ~49.3 mm.)
//
// Row 1 (29 mm = 188 px):  instant speed (left, whatever is left) | bearing
//                          ring, SQUARE (29 mm = 188 px wide)
// Row 2 (all that remains,100 px): four equal cells — max speed, session
//                          time, wind speed, bearing
// No hint bar: the buttons are on the right edge of the enclosure.
static const int SCR_W = 480;                 // panel width at rotation 1
static const int SCR_H = 320;                 // panel height at rotation 1
static const int MM = 6.49;                   // px per millimetre (nominal)
static const int TOP_H = 32;                  // 5 mm top bar (separator at y=31)
static const int BODY_TOP = TOP_H;
static const int ROW1_H = 188;                // 29 mm
static const int ROW2_Y = BODY_TOP + ROW1_H;  // 220
static const int ROW2_H = SCR_H - ROW2_Y;     // 100 (to the panel edge)
static const int BRG_W = ROW1_H;              // square bearing cell: 188
static const int SPEED_W = SCR_W - BRG_W;     // 292: instant-speed cell
static const int CELL4_W = SCR_W / 4;         // 120: row-2 cells

// Text anchors derived from the cells above.
static const int SPEED_LABEL_Y = BODY_TOP + 10;                  // 42
static const int SPEED_VALUE_Y = BODY_TOP + ROW1_H * 2 / 3;      // 157
static const int ROW2_LABEL_Y = ROW2_Y + 12;                     // 232
static const int ROW2_VALUE_Y = ROW2_Y + ROW2_H * 3 / 5;         // 280

static void drawMainGrid()
{
  // Row 1: one split, between the speed cell and the square ring cell.
  tft.drawFastVLine(SPEED_W, BODY_TOP, ROW1_H, GRAY);
  // Row 2: one rule across, then three splits for the four equal cells.
  tft.drawFastHLine(0, ROW2_Y, SCR_W, GRAY);
  for (int i = 1; i < 4; i++) {
    tft.drawFastVLine(i * CELL4_W, ROW2_Y, ROW2_H, GRAY);
  }
}

// ---- Ring cell (col 3, spans both rows) --------------------------------
// "N" glyph on the ring (font 2 — one GLCD step up), centered at (x,y).
// Erase variant passes " N " (wider opaque box clears the ink + margin).
static void drawCompassN(int x, int y, const char* s, uint16_t color)
{
  tft.setTextColor(color, BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(s, x, y, 2);
}

// N-up ring, top-aligned in the cell (4px margins); "N" glyph just inside
// the top — or, in bearing-up view, at the angle where true north sits
// (`nDeg`, 0 = top). Drawn BEFORE the arrows so the solid triangles cover
// it when they overlay; the erase pass may clip ring/N, hence the ring
// lives in the same repaint step as the triangles.
static void drawCompassRing(int cx, int cy, int r, int nDeg)
{
  tft.drawCircle(cx, cy, r, RING_GRAY);
  // Radial ticks every 30°, just inside the ring: short at 30/60/...,
  // twice as long at 90/180/270; 0 (top) is skipped — the N sits there.
  // Ticks are static ring pixels like the N, repainted by the gate's ring
  // pass (arrow erases may clip them).
  for (int d = 30; d < 360; d += 30) {
    const double a = d * M_PI / 180.0;
    const double sx = sin(a), cz = cos(a);
    const int ra = r - 2, rb = (d % 90 == 0) ? r - 9 : r - 5;
    tft.drawLine(cx + (int)(ra * sx), cy - (int)(ra * cz),
                 cx + (int)(rb * sx), cy - (int)(rb * cz), RING_GRAY);
  }
  const double a = nDeg * M_PI / 180.0;
  const int nr = r - 12;
  drawCompassN(cx + (int)(nr * sin(a)), cy - (int)(nr * cos(a)), "N", RING_GRAY);
}

// Upwind no-go arc: ±`half` degrees around `centerDeg` (0 = up), drawn as a
// 12px radial band (r-RING_BAND..r) that swallows the ring inside its sector —
// the arc merges with / overlaps the ring border. Same red as the wind arrow,
// so the "do not sail here" zone and the wind pointer read as one signal.
// Also erases (BG) — same geometry, so an old sector is fully removed.
static void drawNoGoArc(int cx, int cy, int r, int centerDeg, int half, uint16_t color)
{
  for (int d = centerDeg - half; d <= centerDeg + half; d++) {
    const double a = d * M_PI / 180.0;
    const double sx = sin(a), cz = cos(a);
    tft.drawLine(cx + (int)((r - RING_BAND) * sx), cy - (int)((r - RING_BAND) * cz),
                 cx + (int)(r * sx), cy - (int)(r * cz), color);
  }
}

// Solid triangle pointing at `deg` (0 = up, clockwise): tip `h` px beyond
// the base along the bearing, base centered at `baseR` (equilateral width
// minus 5px), plus a 4px-thick / 15px color-matched tail behind the base.
// Erases too.
static void drawNupTriangle(int cx, int cy, int tipR, int baseR, int deg, uint16_t color)
{
  const double a = deg * M_PI / 180.0;
  const double sx = sin(a), cz = cos(a);
  const int h = tipR - baseR;              // altitude
  const double w = h / sqrt(3.0) - 2.5;    // equilateral half-base, -5px total
  const int bx = cx + (int)(baseR * sx), by = cy - (int)(baseR * cz);
  const int tx = cx + (int)(tipR * sx), ty = cy - (int)(tipR * cz);
  const int p1x = bx + (int)(w * cz), p1y = by + (int)(w * sx);
  const int p2x = bx - (int)(w * cz), p2y = by - (int)(w * sx);
  tft.fillTriangle(tx, ty, p1x, p1y, p2x, p2y, color);
  for (int k = 0; k < 4; k++) {            // tail: 4 parallel 1px lines
    const double off = (double)k - 1.5;
    const int ox = (int)(off * cz), oy = (int)(off * sx);
    tft.drawLine(cx + (int)((baseR - 15) * sx) + ox, cy - (int)((baseR - 15) * cz) + oy,
                 bx + ox, by + oy, color);
  }
}

void drawSpeed(TinyGPSPlus &gps)
{
  static const char* unitLabels[SPEED_UNITS] = { "kn", "km/h", "mph" };
  // Session max (RAM only, resets on reboot)
  static double sessionMaxKnots = 0.0;
  static bool hasSessionMax = false;

  const double knots = gps.speed.isValid() ? gps.speed.knots() : 0.0;

  if (gps.speed.isValid()) {
    hasSessionMax = true;
    if (knots > sessionMaxKnots) sessionMaxKnots = knots;
  }

  double value = knots;
  double maxValue = sessionMaxKnots;
  if (config.speedUnit == 1) { value *= 1.852; maxValue *= 1.852; }         // km/h
  else if (config.speedUnit == 2) { value *= 1.15078; maxValue *= 1.15078; } // mph

  // Instant speed cell (row 1, cols 1-2): unit label top-center, value
  // centered below — sized for "88.8" (font 8) + margins top and bottom.
  // A 5-char value ("123.4" in km/h) drops to the narrower font 7.
  const int cx = SPEED_W / 2;
  tft.setTextColor(GRAY, BG);
  tft.setTextDatum(TC_DATUM);
  String label = "Speed (" + String(unitLabels[config.speedUnit]) + ")";
// Font 2 is 16px tall: the label sits just under the top-bar separator
  // (y=30) and must stay ABOVE the width-change wipe below (y>=59), or
  // the wipe clips its bottom rows.
  tft.drawString(label, cx, SPEED_LABEL_Y, 2);

  String spd = gps.speed.isValid() ? String(value, 1) : String("---");
  // Font 8 (75 px) is the largest built-in, but a 5-char value ("123.4" in
  // km/h) can be wider than the 292 px cell; measure it and drop to font 6
  // rather than letting the last glyph spill past the wipe and leave a ghost.
  const uint8_t spdFont = tft.textWidth(spd, 8) <= SPEED_W - 8 ? 8 : 6;
  if (spd != lastSpdStr || spdFont != lastSpdFont) {
    if (spd.length() != lastSpdStr.length() || spdFont != lastSpdFont) {
      // One-off wipe on a width/font change (rare: decade cross, unit toggle,
      // 123.4). Spans the whole cell width so no glyph edge can survive.
      tft.fillRect(1, SPEED_LABEL_Y + 20, SPEED_W - 2,
                   (BODY_TOP + ROW1_H) - (SPEED_LABEL_Y + 24), BG);
    }
    tft.setTextColor(TFT_YELLOW, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(spd, cx, SPEED_VALUE_Y, spdFont);
    lastSpdStr = spd;
    lastSpdFont = spdFont;
  }

  // Row 2, cells 1 and 2: max speed and session time. Each value redraws only
  // on change; a narrower value wipes its cell zone first (no ghosts).
  const int cellMaxX = CELL4_W / 2;
  const int cellSesX = CELL4_W + CELL4_W / 2;
  String maxPadded;
  if (hasSessionMax) {
    maxPadded = " " + String(maxValue, 1) + " ";
  } else {
    maxPadded = " --- ";
  }
  drawCellLabel("Max Speed", cellMaxX, ROW2_LABEL_Y);
  if (maxPadded != lastMaxStr) {
    if (maxPadded.length() < lastMaxStr.length())
      tft.fillRect(3, ROW2_LABEL_Y + 15, CELL4_W - 6, 34, BG);
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(maxPadded, cellMaxX, ROW2_VALUE_Y,
                   fitValueFont(maxPadded, CELL4_W - 8));
    lastMaxStr = maxPadded;
  }

  drawCellLabel("Session time", cellSesX, ROW2_LABEL_Y);
  unsigned long totalSec = millis() / 1000UL;
  unsigned long sesMm = totalSec / 60UL;
  unsigned long sesSs = totalSec % 60UL;
  // "mm:ss" rather than "mm'ss\"": the colon is in font 6's glyph set (the
  // apostrophe and quote are not). Exactly 5 chars, so no padding is needed
  // for a clean overwrite and the measurement is the same string that is drawn.
  char sesBuf[16];
  snprintf(sesBuf, sizeof(sesBuf), "%02lu:%02lu", sesMm, sesSs);
  String sesTxt = String(sesBuf);
  if (sesTxt != lastSesStr) {
    if (sesTxt.length() < lastSesStr.length())
      tft.fillRect(CELL4_W + 3, ROW2_LABEL_Y + 15, CELL4_W - 6, 60, BG);
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(sesTxt, cellSesX, ROW2_VALUE_Y,
                   fitValueFont(sesTxt, CELL4_W - 8));
    lastSesStr = sesTxt;
  }

  // Ring cell (col 3, rows 1-2): N-up ring, top-aligned. Boat = green
  // triangle and wind = same-size red triangle; BOTH tips are tangent to
  // the ring border and share the same radial span, so they can overlap at
  // close angles. The center shows the
  // boat/wind angle: 0 when the boat points directly into the wind source
  // and 180 when it sails dead downwind. The 60° no-go arc (3px, dark red) hugs the ring opposite the wind
  // triangle — where the wind comes FROM. / View mode (R toggle): N-up =
  // true north at the top; bearing-up = the boat always at the top, wind
  // + arc shift bearing-relative and the N glyph sits where north is.
  // Repaints ONLY when an angle / the arc / the mode changed: old shapes
  // erased in BG, stale N cleared, ring+N restored, arc + triangles
  // redrawn (arc under, wind under the boat).
  // Bearing ring: the whole right cell of row 1, square by construction
  // (BRG_W == ROW1_H == 29 mm). The ring is inscribed with an 8 px margin.
  const int cmpR = BRG_W / 2 - 8;              // 86 -> Ø172
  const int cmpCx = SPEED_W + BRG_W / 2;       // 386
  const int cmpCy = BODY_TOP + ROW1_H / 2;     // 126
  const int triR = cmpR * 35 / 100;           // shared triangle altitude
  const int boatTip = cmpR - 1;               // tangent (1px in: no ring flicker)
  const int boatBase = boatTip - triR;
  const int windTip = boatTip;                // shared ring tangent
  const int windBase = boatBase;

  const int brgDeg = gps.course.isValid() ? (int)(gps.course.deg() + 0.5) % 360 : -1;
  const bool sesWind = raceSession.valid && raceSession.windSpeed > 0;
  // Env wind is valid once seen — even a calm 0kn report paints the arrow.
  // (Session wind keeps the >0 gate: frozen 0 there means unknown.)
  const bool envWind = raceEnvWindSeen();
  const bool wndValid = sesWind || envWind;
  const int wndKn = sesWind ? raceSession.windSpeed : raceSession.envWindSpeed;
  const int wndAbs = wndValid ? ((sesWind ? raceSession.windDir : raceSession.envWindDir) + 180) % 360 : -1;
  const bool brgUp = ringBrgUp && brgDeg >= 0;  // no bearing → stay N-up
  // Upwind direction (where the wind comes FROM) — no-go arc center,
  // exactly opposite the downwind tip of the red wind triangle. The arc
  // width (total degrees) is the /nogo setting.
  const int noGoHalf = noGoArcDeg / 2;
  const int upwRaw = sesWind ? raceSession.windDir
                   : (envWind ? raceSession.envWindDir : -1);
  int arcDeg = -1;
  if (upwRaw >= 0) {
    arcDeg = (brgUp && brgDeg >= 0) ? (upwRaw - brgDeg + 360) % 360 : upwRaw % 360;
  }
  const int boatDeg = brgDeg >= 0 ? (brgUp ? 0 : brgDeg) : -1;
  const int windRel = wndAbs >= 0 ? (brgUp ? (wndAbs - brgDeg + 360) % 360 : wndAbs) : -1;
  const int nDeg = brgUp ? (360 - brgDeg) % 360 : 0;
  int windAngleDeg = -1;
  if (brgDeg >= 0 && upwRaw >= 0) {
    const int rawAngle = (brgDeg - upwRaw + 360) % 360;
    windAngleDeg = rawAngle > 180 ? 360 - rawAngle : rawAngle;
  }
  String cenTxt = "---";
  if (windAngleDeg >= 0) {
    cenTxt = String(windAngleDeg);
    while (cenTxt.length() < 3) cenTxt = "0" + cenTxt;
  }

  if (boatDeg != prevBoatDeg || windRel != prevWindDeg ||
      arcDeg != prevArcDeg || noGoHalf != prevNoGoHalf ||
      (int)brgUp != prevRingUp || cenTxt != lastCenStr) {
    if (prevBoatDeg >= 0) drawNupTriangle(cmpCx, cmpCy, boatTip, boatBase, prevBoatDeg, BG);
    if (prevWindDeg >= 0) drawNupTriangle(cmpCx, cmpCy, windTip, windBase, prevWindDeg, BG);
    if (prevArcDeg >= 0) drawNoGoArc(cmpCx, cmpCy, cmpR, prevArcDeg, prevNoGoHalf, BG);
    if (prevNDeg >= 0 && prevNDeg != nDeg) {   // stale N from an old angle
      const double pa = prevNDeg * M_PI / 180.0;
      drawCompassN(cmpCx + (int)((cmpR - 12) * sin(pa)), cmpCy - (int)((cmpR - 12) * cos(pa)), " N ", BG);
    }
    drawCompassRing(cmpCx, cmpCy, cmpR, nDeg); // restore ring + N under the shapes
    if (arcDeg >= 0) drawNoGoArc(cmpCx, cmpCy, cmpR, arcDeg, noGoHalf, ARC_RED);
    if (windRel >= 0) drawNupTriangle(cmpCx, cmpCy, windTip, windBase, windRel, RED);
    if (boatDeg >= 0) drawNupTriangle(cmpCx, cmpCy, boatTip, boatBase, boatDeg, GREEN);
    // Center boat/wind angle, using the next smaller loaded font.
    // TFT_eSPI has no font 3; the loaded classic fonts are 1, 2, 4, 6,
    // 7, and 8. The selected font has no degree glyph, so use the same
    // drawn degree ring as BRG.
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(cenTxt, cmpCx, cmpCy + 3, 2);
    const int cenHalf = tft.textWidth(cenTxt, 2) / 2;
    tft.drawCircle(cmpCx + cenHalf + 3, cmpCy - 1, 2, windAngleDeg >= 0 ? WHITE : BG);
    lastCenStr = cenTxt;
    prevBoatDeg = boatDeg;
    prevWindDeg = windRel;
    prevArcDeg = arcDeg;
    prevNoGoHalf = noGoHalf;
    prevRingUp = (int)brgUp;
    prevNDeg = nDeg;
  }

  // Row 2, cells 3 and 4: wind speed and bearing. They used to be stacked
  // under the ring; the wider panel gives them their own cells, which also
  // gives the ring the full square.
  const int cellWndX = CELL4_W * 2 + CELL4_W / 2;   // 300
  const int cellBrgX = CELL4_W * 3 + CELL4_W / 2;   // 420
  drawCellLabel("Wind (kn)", cellWndX, ROW2_LABEL_Y);
  drawCellLabel("Bearing", cellBrgX, ROW2_LABEL_Y);

  String wndTxt = "---";
  if (wndValid) {
    wndTxt = String(wndKn);
    while (wndTxt.length() < 3) wndTxt = " " + wndTxt;
  }
  if (wndTxt != lastWndStr) {
    lastWndStr = wndTxt;
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(wndTxt, cellWndX, ROW2_VALUE_Y,
                   fitValueFont(wndTxt, CELL4_W - 8));
  }

  // Zero-padded 3 chars (constant width, no ghosting); the degree ring is
  // placed from the measured text width so it clears the digits at any font.
  String degTxt = "---";
  if (brgDeg >= 0) {
    degTxt = String(brgDeg);
    while (degTxt.length() < 3) degTxt = "0" + degTxt;
  }
  if (degTxt != lastBrgStr) {
    lastBrgStr = degTxt;
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    const uint8_t brgFont = fitValueFont(degTxt, CELL4_W - 30); // room for the ring
    tft.drawString(degTxt, cellBrgX - 6, ROW2_VALUE_Y, brgFont);
    tft.drawCircle(cellBrgX - 6 + tft.textWidth(degTxt, brgFont) / 2 + 7,
                   ROW2_VALUE_Y - 10, 2, brgDeg >= 0 ? WHITE : BG);
  }
}

void initScreen() {
  tft.fillScreen(TFT_BLACK);
  prevWifiConnected = TriState::Unknown;
  prevDataConnected = TriState::Unknown;
  prevFix = TriState::Unknown;
  prevFixTile = -1;
  prevDevMode = TriState::Unknown;
  lastMaxStr = "";
  lastSesStr = "";
  lastWndStr = "";
  lastBrgStr = "";
  lastCenStr = "";
  lastSpdStr = "";
  lastSpdFont = 8;
  prevBoatDeg = -2;
  prevWindDeg = -2;
  prevArcDeg = -1;
  prevNoGoHalf = -1;
  prevRingUp = -1;
  prevNDeg = -1;
}

void drawScreenSpeed(TinyGPSPlus &gps, bool requiresInit)
{
  if (requiresInit) initScreen();

  // Normal display
  drawTopBar(gps);
  drawSpeed(gps);

  drawMainGrid();

  // No hint bar: the buttons are on the right edge of the enclosure, so the
  // bottom strip belongs to the readouts now (row 2 runs to the panel edge).
}

void screenSpeedButton(
    Button button,
    ButtonEvent event
) {
    // Left short: advance to the next page
    if (button == Button::Left && event == ButtonEvent::ShortPress) {
        nextScreen();
        return;
    }

    // Left long: jump to the config screen
    if (button == Button::Left && event == ButtonEvent::LongPress) {
        setCurrentPage(PageConfig);
        return;
    }

    // Right short: toggle the ring view N-up <-> bearing-up
    if (button == Button::Right && event == ButtonEvent::ShortPress) {
        ringBrgUp = !ringBrgUp;
        return;
    }

// Right long: jump to the diagnostics screen (RR-only, not part of
    // the L-short cycle)
    if (button == Button::Right && event == ButtonEvent::LongPress) {
      setCurrentPage(PageDiagnostics);
      return;
    }
  }

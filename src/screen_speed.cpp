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
#define ARC_RED TFT_RED  // no-go arc (was DARK_RED: too faint on black)

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

static void drawCellLabel(const char* label, int cx, int y)
{
  tft.setTextColor(GRAY, BG);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(label, cx, y, 2);
}

// ====== LAYOUT ======
// Body = 2 rows x 3 cols under the top bar (1px GRAY lines, same as the
// top separator): row1 cols1-2 = instant speed; row2 col1 = max speed;
// row2 col2 = session time; col3 (rows 1-2) = wind/bearing ring cell.
static const int BODY_TOP = 31;     // below the top-bar separator (y=30)
static const int SPEED_W = 184;     // v-line: speed/max/session | ring cell
static const int MAX_W = 92;        // v-line: max | session (row 2 only)
static const int ROW_MID = 152;     // h-line across cols 1-2 (row 2 = 62px cells)
static const int BODY_BOTTOM = 214; // bottom line, full width

static void drawMainGrid()
{
  tft.drawFastVLine(SPEED_W, BODY_TOP, BODY_BOTTOM - BODY_TOP, GRAY);
  tft.drawFastVLine(MAX_W, ROW_MID, BODY_BOTTOM - ROW_MID, GRAY);
  tft.drawFastHLine(0, ROW_MID, SPEED_W, GRAY);
  tft.drawFastHLine(0, BODY_BOTTOM, tft.width(), GRAY);
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

// Upwind no-go arc: ±`half` degrees around `centerDeg` (0 = up), drawn as
// a 9px radial band (r-8..r) that swallows the ring inside its sector —
// the arc merges with / overlaps the ring border. Also erases (BG).
static void drawNoGoArc(int cx, int cy, int r, int centerDeg, int half, uint16_t color)
{
  for (int d = centerDeg - half; d <= centerDeg + half; d++) {
    const double a = d * M_PI / 180.0;
    const double sx = sin(a), cz = cos(a);
    tft.drawLine(cx + (int)((r - 8) * sx), cy - (int)((r - 8) * cz),
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
  tft.drawString(label, cx, 40, 2);

  String spd = gps.speed.isValid() ? String(value, 1) : String("---");
  const uint8_t spdFont = spd.length() >= 5 ? 7 : 8;
  if (spd != lastSpdStr || spdFont != lastSpdFont) {
    if (spd.length() != lastSpdStr.length() || spdFont != lastSpdFont) {
      // One-off wipe on a width change (rare: decade cross / unit toggle);
      // constant-width overwrites never reach this path. Stays clear of
      // the grid lines (x<184, y<ROW_MID) — no flicker on them.
      tft.fillRect(2, 59, 181, 89, BG);
    }
    tft.setTextColor(TFT_YELLOW, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(spd, cx, 105, spdFont);
    lastSpdStr = spd;
    lastSpdFont = spdFont;
  }

  // Max speed (row 2, col 1) + session time (row 2, col 2): label and
  // value centered in their cells. Values redraw only on change; a
  // narrower value wipes its cell zone first (no ghosts at the edges).
  String maxPadded;
  if (hasSessionMax) {
    maxPadded = " " + String(maxValue, 1) + " ";
  } else {
    maxPadded = " --- ";
  }
  drawCellLabel("Max Speed", MAX_W / 2, ROW_MID + 7);
  if (maxPadded != lastMaxStr) {
    if (maxPadded.length() < lastMaxStr.length())
      tft.fillRect(3, ROW_MID + 25, MAX_W - 6, 28, BG);
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(maxPadded, MAX_W / 2, ROW_MID + 42, 4);
    lastMaxStr = maxPadded;
  }

  drawCellLabel("Session time", MAX_W + (SPEED_W - MAX_W) / 2, ROW_MID + 7);
  unsigned long totalSec = millis() / 1000UL;
  unsigned long sesMm = totalSec / 60UL;
  unsigned long sesSs = totalSec % 60UL;
  char sesBuf[16];
  snprintf(sesBuf, sizeof(sesBuf), "%02lu'%02lu\"", sesMm, sesSs);
  String sesPadded = " " + String(sesBuf) + " ";
  if (sesPadded != lastSesStr) {
    if (sesPadded.length() < lastSesStr.length())
      tft.fillRect(MAX_W + 3, ROW_MID + 25, SPEED_W - MAX_W - 6, 28, BG);
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(sesPadded, MAX_W + (SPEED_W - MAX_W) / 2, ROW_MID + 42, 4);
    lastSesStr = sesPadded;
  }

  // Ring cell (col 3, rows 1-2): N-up ring, top-aligned. Boat = green
  // triangle and wind = same-size red triangle; BOTH tips are tangent to
  // the ring border (experimental: the two arrows share the same radial
  // span, so they can overlap at close angles). The center shows the
  // smallest angular separation between the two displayed markers (not a
  // sailing wind angle off the bow). The 60° no-go arc (3px, dark red) hugs the ring opposite the wind
  // triangle — where the wind comes FROM. / View mode (R toggle): N-up =
  // true north at the top; bearing-up = the boat always at the top, wind
  // + arc shift bearing-relative and the N glyph sits where north is.
  // Repaints ONLY when an angle / the arc / the mode changed: old shapes
  // erased in BG, stale N cleared, ring+N restored, arc + triangles
  // redrawn (arc under, wind under the boat).
  const int cmpL = SPEED_W + 2;               // ring cell inner edges
  const int cmpR = (tft.width() - 2 - cmpL - 8) / 2; // dia = width - 4px/side
  const int cmpCx = cmpL + 4 + cmpR;
  const int cmpCy = BODY_TOP + 4 + cmpR;      // top-aligned
  const int triR = cmpR * 35 / 100;           // shared triangle altitude
  const int boatTip = cmpR - 1;               // tangent (1px in: no ring flicker)
  const int boatBase = boatTip - triR;
  const int windTip = boatTip;                // experimental shared tangent
  const int windBase = boatBase;

  const int brgDeg = gps.course.isValid() ? (int)(gps.course.deg() + 0.5) % 360 : -1;
  const bool sesWind = raceSession.valid && raceSession.windSpeed > 0;
  const bool wndValid = sesWind || raceSession.envWindSpeed > 0;
  const int wndKn = sesWind ? raceSession.windSpeed : raceSession.envWindSpeed;
  const int wndAbs = wndValid ? ((sesWind ? raceSession.windDir : raceSession.envWindDir) + 180) % 360 : -1;
  const bool brgUp = ringBrgUp && brgDeg >= 0;  // no bearing → stay N-up
  // Upwind direction (where the wind comes FROM) — no-go arc center,
  // exactly opposite the downwind tip of the red wind triangle. The arc
  // width (total degrees) is the /nogo setting.
  const int noGoHalf = noGoArcDeg / 2;
  const int upwRaw = sesWind ? raceSession.windDir
                   : (raceSession.envWindSpeed > 0 ? raceSession.envWindDir : -1);
  int arcDeg = -1;
  if (upwRaw >= 0) {
    arcDeg = (brgUp && brgDeg >= 0) ? (upwRaw - brgDeg + 360) % 360 : upwRaw % 360;
  }
  const int boatDeg = brgDeg >= 0 ? (brgUp ? 0 : brgDeg) : -1;
  const int windRel = wndAbs >= 0 ? (brgUp ? (wndAbs - brgDeg + 360) % 360 : wndAbs) : -1;
  const int nDeg = brgUp ? (360 - brgDeg) % 360 : 0;
  int markSepDeg = -1;
  if (boatDeg >= 0 && windRel >= 0) {
    const int rawSep = abs(windRel - boatDeg) % 360;
    markSepDeg = rawSep > 180 ? 360 - rawSep : rawSep;
  }
  String cenTxt = "---";
  if (markSepDeg >= 0) {
    cenTxt = String(markSepDeg);
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
    // Center marker separation, same font as the BRG value. The selected
    // font has no degree glyph, so use the same drawn degree ring as BRG.
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(cenTxt, cmpCx, cmpCy, 4);
    const int cenHalf = tft.textWidth(cenTxt, 4) / 2;
    tft.drawCircle(cmpCx + cenHalf + 5, cmpCy, 2, markSepDeg >= 0 ? WHITE : BG);
    lastCenStr = cenTxt;
    prevBoatDeg = boatDeg;
    prevWindDeg = windRel;
    prevArcDeg = arcDeg;
    prevNoGoHalf = noGoHalf;
    prevRingUp = (int)brgUp;
    prevNDeg = nDeg;
  }

  // Ring values: WIND (kn) (left column) and BRG (right column) — gray
  // font-2 labels, font-4 values under them. Labels sit at y=171, clear of
  // the ring (bottom edge at BODY_TOP+4+2*cmpR = 159); values at 197 stay
  // above the bottom grid line (214). The bearing block (3 digits + degree
  // ring, ~53px) is right-anchored inside the cell and its label centered
  // over it, so label and value line up.
const int valMidL = 221;
const int valMidR = 290;
  tft.setTextColor(GRAY, BG);
  tft.setTextDatum(TC_DATUM);
tft.drawString("Wind (kn)", valMidL, 166, 2);
  tft.drawString("BRG", valMidR, 166, 2);

  String wndTxt = "---";
  if (wndValid) {
    wndTxt = String(wndKn);
    while (wndTxt.length() < 3) wndTxt = " " + wndTxt;
  }
  if (wndTxt != lastWndStr) {
    lastWndStr = wndTxt;
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(wndTxt, valMidL - 8, 187, 4);
  }

  // Zero-padded 3 chars (constant width, no ghosting), font 4 to match the
  // wind value; degree ring right of the digits at a fixed spot.
  String degTxt = "---";
  if (brgDeg >= 0) {
    degTxt = String(brgDeg);
    while (degTxt.length() < 3) degTxt = "0" + degTxt;
  }
  if (degTxt != lastBrgStr) {
    lastBrgStr = degTxt;
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(degTxt, valMidR + 20, 187, 4);
  }
  tft.drawCircle(valMidR + 26, 187, 2, brgDeg >= 0 ? WHITE : BG);
}

void initScreen() {
  tft.fillScreen(TFT_BLACK);
  prevWifiConnected = TriState::Unknown;
  prevDataConnected = TriState::Unknown;
  prevFix = TriState::Unknown;
  prevFixTile = -1;
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

  // Button hints: L/LL on the left, R/RR on the right
  tft.setTextColor(GRAY, BG);
  tft.setTextDatum(BL_DATUM);
  tft.drawString("L Next  LL Cfg", 8, 235, 2);
  tft.setTextDatum(BR_DATUM);
  tft.drawString("R Ring  RR Diag", tft.width() - 8, 235, 2);
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

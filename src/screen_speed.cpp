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
#define DARK_RED 0x8000
#define GRAY 0x7BEF

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

// Right-column cache: values redrawn only on change (no flicker),
// space-padded to overwrite narrower predecessors. Reset in initScreen().
static String lastMaxStr = "";
static String lastSesStr = "";
// Compass cell: angles of the arrows last drawn (-1 = none). Reset on init
// so a redraw never leaves a stale arrow un-erased.
static int prevBrgDeg = -1;
static int prevWndDeg = -1;

static void drawRightValue(const String& padded, int y, uint8_t font, String& last)
{
  if (padded == last) return;
  last = padded;
  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(padded, tft.width() - 4, y, font);
}

static void drawRightLabel(const char* label, int y)
{
  tft.setTextColor(GRAY, BG);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(label, tft.width() - 8, y, 2);
}

// Grid: same 1px GRAY as the top separator (drawTopBar).
// Redrawn every frame over the same pixels — no flicker, no clear needed.
static const int GRID_X = 222;
static const int GRID_TOP = 30;
static const int GRID_BOTTOM = 210;
// East column: 3 equal cells between top and bottom line
// East column: compressed Max/Session cells (50px each) around a tall
// compass cell (80px) that replaced the old Course label/value readout.
static const int GRID_ROW1 = 80;
static const int GRID_ROW2 = 160;

static void drawMainGrid()
{
  tft.drawFastVLine(GRID_X, GRID_TOP, GRID_BOTTOM - GRID_TOP, GRAY);
  tft.drawFastHLine(0, GRID_BOTTOM, tft.width(), GRAY);
  tft.drawFastHLine(GRID_X, GRID_ROW1, tft.width() - GRID_X, GRAY);
  tft.drawFastHLine(GRID_X, GRID_ROW2, tft.width() - GRID_X, GRAY);
}

// ---- Compass cell (middle) --------------------------------------------
// N-up ring with a small north tick — static pixels, overdrawn every frame.
static void drawCompassRing(int cx, int cy, int r)
{
  tft.drawCircle(cx, cy, r, GRAY);
  tft.drawFastVLine(cx, cy - r + 1, 4, GRAY);
}

// Arrow from a short tail to the tip at `r` pixels on bearing `deg`
// (0 = up, clockwise) with a barbed head. Also used to erase (color = BG).
static void drawNupArrow(int cx, int cy, int r, int deg, uint16_t color)
{
  const double a = deg * M_PI / 180.0;
  const double sx = sin(a), cz = cos(a);
  const int tail = (int)(r * 0.35);
  const int bx = cx - (int)(tail * sx), by = cy + (int)(tail * cz);
  const int tx = cx + (int)(r * sx), ty = cy - (int)(r * cz);
  tft.drawLine(bx, by, tx, ty, color);
  // Head: two barbs off the tip, unit tail-ward vector rotated ±28°.
  const double hd = r * 0.45;
  const double ux = -sx, uy = cz;
  for (double s = -28.0; s <= 28.0; s += 56.0) {
    const double w = s * M_PI / 180.0;
    const double rx = ux * cos(w) - uy * sin(w);
    const double ry = ux * sin(w) + uy * cos(w);
    tft.drawLine(tx, ty, tx + (int)(rx * hd), ty + (int)(ry * hd), color);
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

  // Instant speed centered in the west grid slot
  const int cx = GRID_X / 2;
  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(GRAY, BG);
  String label = "SPEED (" + String(unitLabels[config.speedUnit]) + ")";
  tft.drawString(label, cx, 78, 2);

  tft.setTextColor(TFT_YELLOW, BG);
  if (gps.speed.isValid()) {
    String spd = " " + String(value, 1) + " ";
    tft.drawString(spd, cx, 128, 8);
  } else {
    String spd = "  ---  ";
    tft.drawString(spd, cx, 128, 8);
  }

  // Right column: Max speed / compass / Session. Labels gray, values white.
  // No fillRect/clear on the refresh path — stale pixels are overwritten.
  drawRightLabel("Max speed", 34);
  String maxPadded;
  if (hasSessionMax) {
    maxPadded = "  " + String(maxValue, 1) + " ";
  } else {
    maxPadded = "  ---  ";
  }
  drawRightValue(maxPadded, 52, 4, lastMaxStr);

  // Compass cell: N-up ring, COG arrow (white, tip at bearing) + wind arrow
  // (cyan, tip downwind) + wind speed in knots under the ring. Arrows
  // rotate, so each frame the previous ones are erased in BG first, then
  // the ring is refreshed and the new arrows drawn (wind under bearing).
  const int cmpCx = GRID_X + (tft.width() - GRID_X) / 2;
  const int cmpCy = (GRID_ROW1 + GRID_ROW2) / 2;
  const int cmpR = 26;
  drawCompassRing(cmpCx, cmpCy, cmpR);

  const int brgDeg = gps.course.isValid() ? (int)(gps.course.deg() + 0.5) % 360 : -1;
  const bool wndValid = raceSession.valid && raceSession.windSpeed > 0;
  const int wndDeg = wndValid ? (raceSession.windDir + 180) % 360 : -1;

  if (prevBrgDeg >= 0) drawNupArrow(cmpCx, cmpCy, cmpR, prevBrgDeg, BG);
  if (prevWndDeg >= 0) drawNupArrow(cmpCx, cmpCy, cmpR - 5, prevWndDeg, BG);
  drawCompassRing(cmpCx, cmpCy, cmpR); // restore ring/tick under the arrows
  if (wndDeg >= 0) drawNupArrow(cmpCx, cmpCy, cmpR - 5, wndDeg, CYAN);
  if (brgDeg >= 0) drawNupArrow(cmpCx, cmpCy, cmpR, brgDeg, WHITE);
  prevBrgDeg = brgDeg;
  prevWndDeg = wndDeg;

  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(MC_DATUM);
  String wndTxt = wndValid ? (" " + String(raceSession.windSpeed) + " kn ") : String("  ---  ");
  tft.drawString(wndTxt, cmpCx, cmpCy + cmpR + 9, 2);

  drawRightLabel("Session", 163);
  unsigned long totalSec = millis() / 1000UL;
  unsigned long sesMm = totalSec / 60UL;
  unsigned long sesSs = totalSec % 60UL;
  char sesBuf[16];
  snprintf(sesBuf, sizeof(sesBuf), "%02lu'%02lu\"", sesMm, sesSs);
  drawRightValue("  " + String(sesBuf) + " ", 181, 4, lastSesStr);
}

void initScreen() {
  tft.fillScreen(TFT_BLACK);
  prevWifiConnected = TriState::Unknown;
  prevDataConnected = TriState::Unknown;
  prevFix = TriState::Unknown;
  prevFixTile = -1;
  lastMaxStr = "";
  lastSesStr = "";
  prevBrgDeg = -1;
  prevWndDeg = -1;
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
  tft.drawString("RR Diag", tft.width() - 8, 235, 2);
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

// Right long: jump to the diagnostics screen (RR-only, not part of
    // the L-short cycle)
    if (button == Button::Right && event == ButtonEvent::LongPress) {
      setCurrentPage(PageDiagnostics);
      return;
    }
  }

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
// Compass cell: angles of the triangles last drawn. -1 = nothing drawn,
// -2 = cell not rendered yet (sentinel — forces ring+N+arrows on first pass).
static int prevBrgDeg = -2;
static int prevWndDeg = -2;
// Instant speed: last string + font (width changes get a one-off wipe).
static String lastSpdStr = "";
static uint8_t lastSpdFont = 8;

static void drawRightValue(const String& padded, int rightEdge, int y,
                           uint8_t font, String& last)
{
  if (padded == last) return;
  last = padded;
  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(padded, rightEdge, y, font);
}

static void drawRightLabel(const char* label, int rightEdge, int y)
{
  tft.setTextColor(GRAY, BG);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(label, rightEdge, y, 2);
}

// ====== LAYOUT ======
// Body = 2 rows x 3 cols under the top bar (1px GRAY lines, same as the
// top separator): row1 cols1-2 = instant speed; row2 col1 = max speed;
// row2 col2 = session time; col3 (rows 1-2) = wind/bearing ring cell.
static const int BODY_TOP = 31;     // below the top-bar separator (y=30)
static const int SPEED_W = 184;     // v-line: speed/max/session | ring cell
static const int MAX_W = 92;        // v-line: max | session (row 2 only)
static const int ROW_MID = 144;     // h-line across cols 1-2 (speed row +16px)
static const int BODY_BOTTOM = 214; // bottom line, full width

static void drawMainGrid()
{
  tft.drawFastVLine(SPEED_W, BODY_TOP, BODY_BOTTOM - BODY_TOP, GRAY);
  tft.drawFastVLine(MAX_W, ROW_MID, BODY_BOTTOM - ROW_MID, GRAY);
  tft.drawFastHLine(0, ROW_MID, SPEED_W, GRAY);
  tft.drawFastHLine(0, BODY_BOTTOM, tft.width(), GRAY);
}

// ---- Ring cell (col 3, spans both rows) --------------------------------
// N-up ring, top-aligned in the cell (4px margins); "N" glyph just inside
// the top (GLCD renderer — capture-safe). Wind speed + bearing sit below
// the ring. The N is drawn before the arrows so the solid triangles cover
// it when they overlay; the erase pass may clip ring/N, hence the ring
// lives in the same repaint step as the triangles.
static void drawCompassRing(int cx, int cy, int r)
{
  tft.drawCircle(cx, cy, r, GRAY);
  drawLabel1C(cx, cy - r + 8, "N", GRAY);
}

// Solid EQUILATERAL triangle pointing at `deg` (0 = up, clockwise): tip at
// `tipR` from the center, base centered at `baseR` along the same bearing,
// half-width (tipR-baseR)/√3, no stick. Also used to erase (color = BG).
static void drawNupTriangle(int cx, int cy, int tipR, int baseR, int deg, uint16_t color)
{
  const double a = deg * M_PI / 180.0;
  const double sx = sin(a), cz = cos(a);
  const int h = tipR - baseR;              // altitude
  const int w = (int)(h / sqrt(3.0));      // equilateral half-base
  const int bx = cx + (int)(baseR * sx), by = cy - (int)(baseR * cz);
  const int tx = cx + (int)(tipR * sx), ty = cy - (int)(tipR * cz);
  const int p1x = bx + (int)(w * cz), p1y = by + (int)(w * sx);
  const int p2x = bx - (int)(w * cz), p2y = by - (int)(w * sx);
  tft.fillTriangle(tx, ty, p1x, p1y, p2x, p2y, color);
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
  String label = "SPEED (" + String(unitLabels[config.speedUnit]) + ")";
  tft.drawString(label, cx, 42, 2);

  String spd = gps.speed.isValid() ? String(value, 1) : String("---");
  const uint8_t spdFont = spd.length() >= 5 ? 7 : 8;
  if (spd != lastSpdStr || spdFont != lastSpdFont) {
    if (spd.length() != lastSpdStr.length() || spdFont != lastSpdFont) {
      // One-off wipe on a width change (rare: decade cross / unit toggle);
      // constant-width overwrites never reach this path. Stays clear of
      // the grid lines (x<184, y<144) — no flicker on them.
      tft.fillRect(2, 59, 181, 77, BG);
    }
    tft.setTextColor(TFT_YELLOW, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(spd, cx, 98, spdFont);
    lastSpdStr = spd;
    lastSpdFont = spdFont;
  }

  // Max speed (row 2, col 1) + session time (row 2, col 2). Labels gray,
  // values white, redrawn only on change (no flicker).
  String maxPadded;
  if (hasSessionMax) {
    maxPadded = "  " + String(maxValue, 1) + " ";
  } else {
    maxPadded = "  ---  ";
  }
  drawRightLabel("Max speed", MAX_W - 8, ROW_MID + 12);
  drawRightValue(maxPadded, MAX_W - 7, ROW_MID + 32, 4, lastMaxStr);

  drawRightLabel("Session", SPEED_W - 8, ROW_MID + 12);
  unsigned long totalSec = millis() / 1000UL;
  unsigned long sesMm = totalSec / 60UL;
  unsigned long sesSs = totalSec % 60UL;
  char sesBuf[16];
  snprintf(sesBuf, sizeof(sesBuf), "%02lu'%02lu\"", sesMm, sesSs);
  drawRightValue("  " + String(sesBuf) + " ", SPEED_W - 1, ROW_MID + 32, 4, lastSesStr);

  // Ring cell (col 3, rows 1-2): N-up ring, top-aligned. Boat = HALF-size
  // green triangle, tip tangent to the ring border (bearing, base pushed
  // out along it); wind = same size red triangle, tip tangent to the
  // boat's base (no overlap: the two stack radially), tip downwind.
  // Triangles repaint ONLY when an angle actually changed: old ones
  // erased in BG, ring+N restored, both redrawn (wind under the boat).
  // Unchanged frames draw nothing — that keeps ring/cell edges stable.
  const int cmpL = SPEED_W + 2;               // ring cell inner edges
  const int cmpR = (tft.width() - 2 - cmpL - 8) / 2; // dia = width - 4px/side
  const int cmpCx = cmpL + 4 + cmpR;
  const int cmpCy = BODY_TOP + 4 + cmpR;      // top-aligned
  const int triR = cmpR * 3 / 10;             // shared triangle altitude
  const int boatTip = cmpR - 1;               // tangent (1px in: no ring flicker)
  const int boatBase = boatTip - triR;
  const int windTip = boatBase;               // tangent to the boat base
  const int windBase = boatBase - triR;

  const int brgDeg = gps.course.isValid() ? (int)(gps.course.deg() + 0.5) % 360 : -1;
  const bool sesWind = raceSession.valid && raceSession.windSpeed > 0;
  const bool wndValid = sesWind || raceSession.envWindSpeed > 0;
  const int wndKn = sesWind ? raceSession.windSpeed : raceSession.envWindSpeed;
  const int wndDeg = wndValid ? ((sesWind ? raceSession.windDir : raceSession.envWindDir) + 180) % 360 : -1;

  if (brgDeg != prevBrgDeg || wndDeg != prevWndDeg) {
    if (prevBrgDeg >= 0) drawNupTriangle(cmpCx, cmpCy, boatTip, boatBase, prevBrgDeg, BG);
    if (prevWndDeg >= 0) drawNupTriangle(cmpCx, cmpCy, windTip, windBase, prevWndDeg, BG);
    drawCompassRing(cmpCx, cmpCy, cmpR); // restore ring + N under the arrows
    if (wndDeg >= 0) drawNupTriangle(cmpCx, cmpCy, windTip, windBase, wndDeg, RED);
    if (brgDeg >= 0) drawNupTriangle(cmpCx, cmpCy, boatTip, boatBase, brgDeg, GREEN);
    prevBrgDeg = brgDeg;
    prevWndDeg = wndDeg;
  }

  // Ring values: WND (left) and BRG (right), gray font-2 labels, GLCD
  // (font-1 via drawLabel1 — capture-safe) values fixed at 3 chars.
  const int valMidL = (cmpL + cmpCx) / 2;
  const int valMidR = (cmpCx + tft.width() - 2) / 2;
  tft.setTextColor(GRAY, BG);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("WND", valMidL, 162, 2);
  tft.drawString("BRG", valMidR, 162, 2);

  String wndTxt = "---";
  if (wndValid) {
    wndTxt = String(wndKn);
    while (wndTxt.length() < 3) wndTxt = " " + wndTxt;
  }
  if (wndTxt != lastWndStr) {
    lastWndStr = wndTxt;
    drawLabel1(valMidL - 9, 184, wndTxt.c_str(), WHITE);
  }

  // Zero-padded 3 chars (constant width, no ghosting); degree ring drawn
  // right of the digits at a fixed spot.
  String degTxt = "---";
  if (brgDeg >= 0) {
    degTxt = String(brgDeg);
    while (degTxt.length() < 3) degTxt = "0" + degTxt;
  }
  if (degTxt != lastBrgStr) {
    lastBrgStr = degTxt;
    drawLabel1(valMidR, 184, degTxt.c_str(), WHITE);
  }
  tft.drawCircle(valMidR + 20, 186, 2, brgDeg >= 0 ? WHITE : BG);
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
  lastSpdStr = "";
  lastSpdFont = 8;
  prevBrgDeg = -2;
  prevWndDeg = -2;
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

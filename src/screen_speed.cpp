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

// ====== FIX FRESHNESS ======
// A fix is only a FIX while it is RECENT. TinyGPSPlus's isValid() means "the
// last sentence it parsed was sound", not "there is a fix now": it never
// returns to false when the sky goes away, and it outlives a switch from MOCK
// to REAL because the mock's last synthetic sentence is still inside the same
// shared object. That is why a real receiver with no fix at all kept a GREEN
// FX tile and a bench speed (2026-11-02). Two guards:
//   - age <= FX_STALE_MS: the same 5 s the race engine already calls stale
//     (race_run.cpp) - both sources deliver at 1 Hz, so it cannot flap in use;
//   - age <= sinceSwitch: after the source flips, nothing the OLD source said
//     counts, so the panel is honest at once instead of coasting.
// Everything derived from the fix (speed, course, satellites) must ask this,
// not isValid(), or a readout outlives the fix it came from.
static const unsigned long FX_STALE_MS = 5000;

static bool fixFresh(TinyGPSPlus &gps)
{
  static bool srcKnown = false;
  static bool srcMock = false;
  static unsigned long srcSwitchAt = 0;
  const bool mockSrc = gpsMockActive();
  if (!srcKnown || mockSrc != srcMock) {
    srcKnown = true;
    srcMock = mockSrc;
    srcSwitchAt = millis();
  }
  const unsigned long sinceSwitch = millis() - srcSwitchAt;
  const unsigned long age = gps.location.isValid() ? gps.location.age() : 0xFFFFFFFFUL;
  return age <= FX_STALE_MS && age <= sinceSwitch;
}

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

  // Fix Icon (yellow FX tile while the mock drives the fix instead of GPS;
  // green only for a RECENT real fix, red otherwise - see fixFresh()).
  icon_x += tile_width + tile_spacing;
  const bool mockSrc = gpsMockActive();
  const int fixTile = mockSrc ? 2 : (fixFresh(gps) ? 1 : 0);
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

  int sats = (fixFresh(gps) && gps.satellites.isValid()) ? gps.satellites.value() : 0;

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
// View tag in the ring cell's top-left corner ("North" / "Head" / "Wind").
// Padded to 5 chars so the opaque background erases the previous word: the
// words differ in length and there is no fillRect on the 200 ms path.
static String lastViewStr = "";
// Compass cell: angles of the triangles last drawn (view-relative: in
// heading-up the boat is 0, in wind-up the wind SOURCE is 0). -1 = nothing
// drawn, -2 = cell not rendered yet (sentinel — forces ring+N+arrows on first
// pass). prevNDeg tracks the angle where the N glyph was last painted
// (stale-N clear).
static int prevBoatDeg = -2;
static int prevWindDeg = -2;
static int prevArcDeg = -1;  // no-go arc center last drawn (-1 = none)
static int prevNoGoHalf = -1; // no-go half-width last drawn (-1 = none)
static int prevNDeg = -1;
// Ring view, one rotation offset for the whole dial:
//   0 north-up    - true north at the top (rot 0)
//   1 heading-up  - the boat's own COG at the top (rot = COG)
//   2 wind-up     - the direction the wind comes FROM at the top, so the
//                   no-go arc is centred at 12 o'clock (rot = wind source)
// `2S` cycles them; RAM only (a reboot comes back to north-up). A view whose
// input is missing right now (no COG, no wind) draws as north-up while it is
// missing and returns by itself when the input does - the MODE is never
// silently changed by a data gap.
static int ringView = 0;
// The view the dial ACTUALLY draws (0/1/2), latched by the last pass. It equals
// ringView except while that view's input is missing, which is exactly the
// distinction /status exposes: a north-up picture under a "wind" mode is
// information, not a fault.
static int ringDrawn = 0;
// Availability from the last draw pass, so the button can SKIP a view that
// cannot be drawn instead of landing on a press with no visible effect.
static bool ringCanHeading = false;
static bool ringCanWind = false;
// Instant speed: last string + font (width changes get a one-off wipe).
static String lastSpdStr = "";
static uint8_t lastSpdFont = 8;
static int lastSpdW = -1;    // last drawn readout width  (px)
static int lastSpdH = -1;    // last drawn readout height (px)

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
// Row 2 (all that remains,100 px): four cells — max speed, session time, wind
// speed, bearing.
// The hint strip (below) takes 12 mm off the right edge, so these cells share
// 402 px instead of 480.
static const int SCR_W = 480;                 // panel width at rotation 1
static const int SCR_H = 320;                 // panel height at rotation 1
static const int MM = 6.49;                   // px per millimetre (nominal)
static const int TOP_H = 32;                  // 5 mm top bar (separator at y=31)
static const int BODY_TOP = TOP_H;
static const int ROW1_H = 188;                // 29 mm
static const int ROW2_Y = BODY_TOP + ROW1_H;  // 220
static const int ROW2_H = SCR_H - ROW2_Y;     // 100 (to the panel edge)

// ---- hints strip (2026-11-03, branch feature/hint-bar) --------------------
// A thin column of press marks against the RIGHT EDGE, where the buttons are.
// The page has had no hint bar at all since the buttons moved to the edge
// (2026-11-01); this is not that bar come back — it carries no words. A name
// written beside a button is read as that button's job, and these buttons
// change job with the page (MAIN/ RACE/ DIAGNOSTICS/ CONFIG), so any word
// would be wrong on most pages. What does NOT change is which PRESS is which:
// short or long. So the strip is one group per physical button — S above L —
// placed at the button's position on the edge, and nothing else.
//
// 3 mm wide (20 px: one font-2 glyph plus air). At 12 mm the words pushed the
// ring cell down to o124 and the dial's own number to font 4; at 3 mm the dial
// keeps its o172 and only the speed cell gives up 20 px (its segmented readout
// re-fits, so the digits just come out a little shorter, never clipped).
static const int HINT_W = 20;                      // 3 mm
static const int HINT_X = SCR_W - HINT_W;          // 460: the column's left edge
static const int BODY_W = HINT_X;                  // 460: usable body width
// Row 1: the speed cell keeps the room its segmented digits need, the ring cell
// takes the rest — BRG_W = 188, so the dial is untouched at o172.
static const int SPEED_W = 272;
static const int BRG_W = BODY_W - SPEED_W;         // 188
// Row 2 is NOT four equal cells: the session clock needs room for four font-6
// digits plus a real 15px colon ("12:34" = 123px), which the 120px cell could
// not hold — that is why it once fell back to font 4 (and then to a tiny font-2
// colon). 136px gives the clock ~6px of air each side; max, wind and bearing
// take the cut for the column.
static const int ROW2_EDGE[5] = {0, 108, 244, 344, BODY_W};
static const int ROW2_CX[4] = {54, 176, 294, 402};   // max | session | wind | brg

// Text anchors derived from the cells above.
static const int SPEED_LABEL_Y = BODY_TOP + 10;                  // 42
// Centre of the space BELOW the label (label is font 2, 16px): (58+220)/2.
// Font 8 is 75px tall, so this leaves equal air above and below it.
static const int SPEED_VALUE_Y = (SPEED_LABEL_Y + 16 + BODY_TOP + ROW1_H) / 2;  // 139
static const int ROW2_LABEL_Y = ROW2_Y + 12;                     // 232
static const int ROW2_VALUE_Y = ROW2_Y + ROW2_H * 3 / 5;         // 280

static void drawMainGrid()
{
  // Row 1: one split, between the speed cell and the square ring cell.
  tft.drawFastVLine(SPEED_W, BODY_TOP, ROW1_H, GRAY);
  // Row 2: one rule across the BODY (it stops at the hint column), then the
  // splits for its four cells (the session cell is wider, see ROW2_EDGE).
  tft.drawFastHLine(0, ROW2_Y, BODY_W, GRAY);
  for (int i = 1; i < 4; i++) {
    tft.drawFastVLine(ROW2_EDGE[i], ROW2_Y, ROW2_H, GRAY);
  }
  // The hint column's own edge — it runs the whole body, so the marks read as
  // a column of the instrument rather than a box drawn around some text.
  tft.drawFastVLine(HINT_X, BODY_TOP, SCR_H - BODY_TOP, GRAY);
}

// ---- Hints strip -----------------------------------------------------------
// One word per PHYSICAL button, written DOWN the column and centred on that
// button (the three are evenly spaced down the edge, so they sit at the thirds
// of the body below the top bar).
//
// Two things make this fit in 3 mm. Vertical writing: letters stacked upright,
// so a word costs WIDTH = one glyph and HEIGHT = 16 px per letter ("STATS" is
// 8 px across, 80 px down) — a horizontal word would need ~50 px across, which
// is the whole point of the thin column. And the short press only: it is the
// primary layer, the long press is the secondary one (CONFIG / DIAGNOSTICS /
// partial reset), and writing both would need 160 px of a 96 px slot.
//
// The words are MAIN's. They are page-specific on purpose — that is what makes
// them worth printing — so a second page needs its own three words here, not a
// shared list.
// Static text: painted once per page init, never on the 200 ms path.
static void drawHints()
{
  static const char *words[3] = { "NEXT", "VIEW", "STATS" };
  const int cx = HINT_X + HINT_W / 2;
  const int bodyH = SCR_H - BODY_TOP;                    // 288
  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(MC_DATUM);
  for (int i = 0; i < 3; i++) {
    const char *w = words[i];
    const int n = strlen(w);
    const int cy = BODY_TOP + (bodyH * (2 * i + 1)) / 6;  // 80 / 176 / 272
    const int top = cy - (n * 16) / 2 + 8;                // first glyph centre
    for (int k = 0; k < n; k++) {
      tft.drawString(String(w[k]), cx, top + k * 16, 2);
    }
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
  const int nr = r - RING_BAND - 12;  // inboard of the no-go band
  drawCompassN(cx + (int)(nr * sin(a)), cy - (int)(nr * cos(a)), "N", RING_GRAY);
}

// Upwind no-go arc: ±`half` degrees around `centerDeg` (0 = up), a solid
// RING_BAND-thick band (r-RING_BAND..r) that swallows the ring inside its
// sector. Drawn as filled quads between consecutive degrees, NOT as 1px radial
// lines: lines leave a sparse/dithered edge whenever the spacing between them
// approaches the pen width, which is what "the band is not solid" was.
// Same red as the wind arrow. Also erases (BG) — the quads tile the sector
// exactly, so an old sector is fully removed.
static void drawNoGoArc(int cx, int cy, int r, int centerDeg, int half, uint16_t color)
{
  for (int d = centerDeg - half; d < centerDeg + half; d++) {
    const double a0 = d * M_PI / 180.0;
    const double a1 = (d + 1) * M_PI / 180.0;
    const int ri = r - RING_BAND;
    const int xi0 = cx + (int)(ri * sin(a0)), yi0 = cy - (int)(ri * cos(a0));
    const int xo0 = cx + (int)(r * sin(a0)),  yo0 = cy - (int)(r * cos(a0));
    const int xi1 = cx + (int)(ri * sin(a1)), yi1 = cy - (int)(ri * cos(a1));
    const int xo1 = cx + (int)(r * sin(a1)),  yo1 = cy - (int)(r * cos(a1));
    // Two triangles make the quad; neighbours share an edge, so the band is
    // continuous along its whole length.
    tft.fillTriangle(xi0, yi0, xo0, yo0, xo1, yo1, color);
    tft.fillTriangle(xi0, yi0, xo1, yo1, xi1, yi1, color);
  }
}

// Solid triangle pointing at `deg` (0 = up, clockwise): tip `h` px beyond
// the base along the bearing, base centered at `baseR` (equilateral width
// minus 5px). No tail any more (2026-11-02): the triangles alone read fine at
// this size and the sticks made the ring busy. Erases too.
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
}

// --------------------------------------------------
// SESSION / PARTIAL STATISTICS  (RAM only, reset on reboot)
//
// The receiver reports speed at ~1 Hz, so the average is time-weighted by the
// gap between valid samples: a dropped fix lengthens the gap instead of adding
// a sample of 0 knots, and a gap longer than 5 s is not counted at all (nothing
// was measured, so counting it would drag the average down for no reason).
//
// PARTIAL = a sub-session inside the session, with its own start, max and
// average: 3L starts a new one (the session is untouched), 3S toggles the two
// row-2 readouts between max/session and avg/partial.
// --------------------------------------------------
static double stSesMaxKn = 0.0, stSesSumKn = 0.0;
static unsigned long stSesMs = 0;
static double stParMaxKn = 0.0, stParSumKn = 0.0;
static unsigned long stParMs = 0;
static unsigned long stParStartMs = 0;
static unsigned long stLastSampleMs = 0;
static bool stHasFix = false;
static bool showPartial = false;

static void statsSample(double kn, bool valid)
{
  const unsigned long now = millis();
  if (!valid) { stLastSampleMs = now; return; }
  if (stLastSampleMs) {
    const unsigned long dt = now - stLastSampleMs;
    if (dt <= 5000) {
      stSesSumKn += kn * dt;
      stSesMs += dt;
      stParSumKn += kn * dt;
      stParMs += dt;
    }
  }
  stLastSampleMs = now;
  stHasFix = true;
  if (kn > stSesMaxKn) stSesMaxKn = kn;
  if (kn > stParMaxKn) stParMaxKn = kn;
}

static double statsSessionMaxKn() { return stSesMaxKn; }
static double statsPartialAvgKn() { return stParMs ? stParSumKn / stParMs : 0.0; }
static double statsPartialMaxKn() { return stParMaxKn; }
static unsigned long statsSessionMs() { return millis(); }                    // since boot
static unsigned long statsPartialMs() { return stParStartMs ? millis() - stParStartMs : millis(); }

static void partialReset()
{
  stParMaxKn = 0.0;
  stParSumKn = 0.0;
  stParMs = 0;
  stParStartMs = millis();
}

// Force both readout cells to repaint (label + value) on the next pass.
static void statsForceRedraw()
{
  lastMaxStr = "";
  lastSesStr = "";
}

// --------------------------------------------------
// INSTANT SPEED — hand-drawn 7-segment readout
//
// TFT_eSPI's largest built-in digit face is font 8 at 75px, and setTextSize()
// only scales font 1, so the readout was leaving most of a 188px cell empty.
// These segments are drawn as filled polygons at ANY height, so the size
// follows the cell instead of the font table. The look is deliberately font 8's
// (a 7-segment face), just bigger.
//
// Segments, clockwise from the top: a, b, c, d, e, f, g (g = middle).
// --------------------------------------------------

// Horizontal bar centred on y, with chamfered ends.
static void segBarH(int xa, int xb, int y, int t, uint16_t c)
{
  const int h = t / 2;
  if (xb - xa < 2 * h + 2) return;
  tft.fillRect(xa + h, y - h, xb - xa - 2 * h, t, c);
  tft.fillTriangle(xa, y, xa + h, y - h, xa + h, y + h, c);
  tft.fillTriangle(xb, y, xb - h, y - h, xb - h, y + h, c);
}

// Vertical bar centred on x, with chamfered ends.
static void segBarV(int x, int ya, int yb, int t, uint16_t c)
{
  const int h = t / 2;
  if (yb - ya < 2 * h + 2) return;
  tft.fillRect(x - h, ya + h, t, yb - ya - 2 * h, c);
  tft.fillTriangle(x, ya, x - h, ya + h, x + h, ya + h, c);
  tft.fillTriangle(x, yb, x - h, yb - h, x + h, yb - h, c);
}

// Draw an explicit segment mask. Split out from segDigit so the caller can
// paint ALL segments in the background first: a segmented glyph has no
// bounding box of its own, so without that a digit going 8 -> 1 would keep its
// neighbour's bars.
static void segDigitMask(int x, int y, int w, int h, uint8_t m, uint16_t c)
{
  if (!m) return;

  const int t = (h / 10 > 4) ? h / 10 : 4;   // stroke thickness (bolder)
  const int y0 = y + t / 2;
  const int y1 = y + h / 2;
  const int y2 = y + h - t / 2;
  const int xl = x + t / 2;
  const int xr = x + w - t / 2;

  if (m & 0b0000001) segBarH(xl, xr, y0, t, c);  // a
  if (m & 0b0000010) segBarV(xr, y0, y1 - 1, t, c);  // b
  if (m & 0b0000100) segBarV(xr, y1 + 1, y2, t, c);  // c
  if (m & 0b0001000) segBarH(xl, xr, y2, t, c);  // d
  if (m & 0b0010000) segBarV(xl, y1 + 1, y2, t, c);  // e
  if (m & 0b0100000) segBarV(xl, y0, y1 - 1, t, c);  // f
  if (m & 0b1000000) segBarH(xl, xr, y1, t, c);  // g
}

static void segDigit(int x, int y, int w, int h, char ch, uint16_t c)
{
  uint8_t m;
  switch (ch) {
    case '0': m = 0b0111111; break;
    case '1': m = 0b0000110; break;
    case '2': m = 0b1011011; break;
    case '3': m = 0b1001111; break;
    case '4': m = 0b1100110; break;
    case '5': m = 0b1101101; break;
    case '6': m = 0b1111101; break;
    case '7': m = 0b0000111; break;
    case '8': m = 0b1111111; break;
    case '9': m = 0b1101111; break;
    case '-': m = 0b1000000; break;   // middle bar: the no-fix placeholder
    default:  m = 0; break;           // space / anything else: nothing
  }
  if (!m) return;
  segDigitMask(x, y, w, h, m, c);
}

// Cell width of one glyph at height h.
//   integer digits 0.58h, but "1" only 0.30h — a 7-seg "1" is two bars on the
//   RIGHT of a full-width cell, so treating it as full width made "1.5" look
//   right-aligned.
//   FRACTION digits are always 0.58h, even a "1": the decimal part must not
//   change the block's width, or the whole value slides sideways when the
//   tenths happen to be a 1 (4.1 vs 4.3). Only the integer part is allowed to
//   move the centring.
//   decimal point 0.18h, 0.06h between glyphs.
static int segAdvance(char c, int h, bool fractional)
{
  if (c == '.') return (h * 18) / 100;
  if (c == '1' && !fractional) return (h * 30) / 100;
  return (h * 58) / 100;
}

static int segWidth(const String& s, int h)
{
  const int gap = (h * 6) / 100;
  bool frac = false;
  int w = 0;
  for (int i = 0; i < (int)s.length(); i++) {
    if (s[i] == '.') frac = true;
    w += segAdvance(s[i], h, frac);
    if (i + 1 < (int)s.length()) w += gap;
  }
  return w;
}

// Biggest height that fits BOTH the width and the height available. Steps down
// in 2px increments, so a 5th character shrinks the whole readout instead of
// clipping it.
static int segFitHeight(const String& s, int maxW, int maxH)
{
  int h = maxH;
  while (h > 40 && segWidth(s, h) > maxW) h -= 2;
  return h;
}

// ---- Incremental readout --------------------------------------------------
// Repainting every bar of every digit on each refresh is what makes a
// segmented display flicker: each update is ~30 polygon fills and the panel
// scan-out catches them mid-draw. So the previous frame's glyph cells are kept
// and only the differences are painted:
//   * a glyph whose character did not change is not touched at all;
//   * inside a changed glyph only the segments that turned on/off are drawn.
// A geometry change (different width/height, i.e. the readout re-centred)
// invalidates the cache: the caller clears the band and everything is redrawn.
#define SEG_STROKE1 0b10000000   // the centred "1" stroke, not a segment set

struct SegCell {
  int x, y, w, h;
  char ch;
};

static SegCell segPrev[12];
static int segPrevN = 0;
static bool segPrevValid = false;

static uint8_t segMaskFor(char ch)
{
  switch (ch) {
    case '0': return 0b0111111;
    case '1': return SEG_STROKE1;
    case '2': return 0b1011011;
    case '3': return 0b1001111;
    case '4': return 0b1100110;
    case '5': return 0b1101101;
    case '6': return 0b1111101;
    case '7': return 0b0000111;
    case '8': return 0b1111111;
    case '9': return 0b1101111;
    case '-': return 0b1000000;
    default:  return 0;
  }
}

// Paint one cell from scratch (or, with a mask, only some of its segments).
static void segPaintCell(const SegCell& c, uint8_t mask, uint16_t color)
{
  const int t = (c.h / 10 > 4) ? c.h / 10 : 4;
  if (c.ch == '.') {
    const int dotW = (c.h * 18) / 100;
    const int dot = (dotW / 2 > 3) ? dotW / 2 : 3;
    tft.fillRect(c.x + dotW / 2 - dot / 2, c.y + c.h - dot, dot, dot, color);
    return;
  }
  if (mask & SEG_STROKE1) {
    segBarV(c.x + c.w / 2, c.y + t / 2, c.y + c.h - t / 2, t, color);
  }
  // 0b1111111 = all seven segments; SEG_STROKE1 (bit 7) is stripped here so a
  // '1' does not also try to draw every bar. (0b0111111 would drop 'g'.)
  segDigitMask(c.x, c.y, c.w, c.h, mask & 0b1111111, color);
}

// Wipe a cell: the 1px-margin box covers chamfer overshoot and a dot whose size
// follows the height.
static void segEraseCell(const SegCell& c)
{
  if (c.ch == '.') {
    const int dotW = (c.h * 18) / 100;
    tft.fillRect(c.x - 1, c.y + c.h - dotW - 3, dotW + 2, dotW + 4, BG);
  } else {
    tft.fillRect(c.x - 1, c.y - 1, c.w + 2, c.h + 2, BG);
  }
}

// Draw the readout for `s`, touching only what differs from the previous frame
// unless `wipe` (geometry changed and the caller has already cleared the band).
static void drawSegReadoutDiff(const String& s, int cx, int cy, int h,
                               uint16_t color, bool wipe)
{
  if (!s.length() || h <= 0) return;

  const int gap = (h * 6) / 100;
  SegCell next[12];
  int n = 0;
  int x = cx - segWidth(s, h) / 2;
  const int y = cy - h / 2;
  bool frac = false;
  for (int i = 0; i < (int)s.length() && n < 12; i++) {
    const char c = s[i];
    if (c == '.') frac = true;
    const int cw = segAdvance(c, h, frac);
    next[n].x = x;
    next[n].y = y;
    next[n].w = cw;
    next[n].h = h;
    next[n].ch = c;
    n++;
    x += cw + gap;
  }

  if (wipe || !segPrevValid) {
    for (int i = 0; i < n; i++) segPaintCell(next[i], segMaskFor(next[i].ch), color);
  } else {
    for (int i = 0; i < n; i++) {
      if (i < segPrevN && segPrev[i].ch == next[i].ch) continue;   // unchanged glyph
      const uint8_t oldM = (i < segPrevN) ? segMaskFor(segPrev[i].ch) : 0;
      const uint8_t newM = segMaskFor(next[i].ch);
      const bool sameBox = i < segPrevN &&
                           segPrev[i].x == next[i].x &&
                           segPrev[i].w == next[i].w &&
                           segPrev[i].h == next[i].h;
      if (sameBox && segPrev[i].ch != '.' && next[i].ch != '.') {
        // Same digit cell, different digit: paint ONLY the segments that turn
        // off (in BG) and on (in colour). Everything that stays lit is never
        // touched, so a 0 -> 8 lights just the middle bar.
        const uint8_t offM = oldM & ~newM;
        const uint8_t onM = newM & ~oldM;
        if (offM) segPaintCell(segPrev[i], offM, BG);
        if (onM) segPaintCell(next[i], onM, color);
      } else {
        // Different kind of cell (digit <-> point) or a moved/resized box.
        if (i < segPrevN) segEraseCell(segPrev[i]);
        segPaintCell(next[i], newM, color);
      }
    }
    for (int i = n; i < segPrevN; i++) segEraseCell(segPrev[i]);    // string got shorter
  }

  for (int i = 0; i < n; i++) segPrev[i] = next[i];
  segPrevN = n;
  segPrevValid = true;
}

static const int TRI_HALF_DEG = 16;   // angular half-width of a triangle's base

// True when two bearings are within `tol` degrees of each other (any side).
static bool angleWithin(int a, int b, int tol)
{
  if (a < 0 || b < 0) return false;
  int d = ((a - b) % 360 + 360) % 360;
  return d <= tol || d >= 360 - tol;
}

void drawSpeed(TinyGPSPlus &gps)
{
  static const char* unitLabels[SPEED_UNITS] = { "kn", "km/h", "mph" };
  // Same freshness rule as the FX tile: nothing derived from the fix may
  // outlive it. A stale speed is worse than no speed - it reads as a real one.
  const bool fixOk = fixFresh(gps) && gps.speed.isValid();
  const double knots = fixOk ? gps.speed.knots() : 0.0;
  statsSample(knots, fixOk);

  double value = knots;
  // Cell 1: the session's max, or the PARTIAL's average (3S toggles).
  double maxValue = showPartial ? statsPartialAvgKn() : statsSessionMaxKn();
  double avgPartial = statsPartialAvgKn();
  if (config.speedUnit == 1) { value *= 1.852; maxValue *= 1.852; avgPartial *= 1.852; }
  else if (config.speedUnit == 2) { value *= 1.15078; maxValue *= 1.15078; avgPartial *= 1.15078; }
  (void)avgPartial;

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

  String spd = fixOk ? String(value, 1) : String("---");
  // Segmented readout: the height follows the cell, and every glyph is centred
  // as a block. So a change in TOTAL WIDTH or HEIGHT moves ALL the glyphs, and
  // clearing per glyph at the new positions cannot erase the ink at the old
  // ones — the old digits stay behind as ghosts. A '1' is narrower than the
  // other digits, so this fires whenever a 1 enters or leaves the number, not
  // only when the character count changes.
  const int spdMaxW = SPEED_W - 14;
  const int spdH = segFitHeight(spd, spdMaxW, 130);
  const int spdW = segWidth(spd, spdH);
  if (spd != lastSpdStr || spdW != lastSpdW || spdH != lastSpdH) {
    const bool geometryChanged = (spdW != lastSpdW || spdH != lastSpdH);
    if (geometryChanged) {
      // Re-centring moves EVERY glyph, so the old ink cannot be erased by
      // painting at the new positions: clear the whole value band.
      tft.fillRect(1, SPEED_LABEL_Y + 18, SPEED_W - 2,
                   (BODY_TOP + ROW1_H - 4) - (SPEED_LABEL_Y + 18), BG);
    }
    // Only the glyphs (and the segments inside them) that changed are painted.
    drawSegReadoutDiff(spd, cx, SPEED_VALUE_Y, spdH, TFT_YELLOW, geometryChanged);
    lastSpdStr = spd;
    lastSpdW = spdW;
    lastSpdH = spdH;
    lastSpdFont = 8;   // font 8 equivalent; kept so initScreen() resets cleanly
  }

  // Row 2, cells 1 and 2: max speed and session time. Each value redraws only
  // on change; a narrower value wipes its cell zone first (no ghosts).
  const int cellMaxX = ROW2_CX[0];
  const int cellSesX = ROW2_CX[1];
  const bool hasValue = stHasFix;
  String maxPadded;
  if (hasValue) {
    maxPadded = " " + String(maxValue, 1) + " ";
  } else {
    maxPadded = " --- ";
  }
  // The two row-2 mode labels are the only text whose glyph widths change with
  // the 3S toggle ("Session time" vs "Partial time" are both 12 chars, but not
  // the same 12). Wipe their band once on a mode change so the old string's
  // edges cannot survive.
  static int lastLabelMode = -1;
  const int labelMode = showPartial ? 1 : 0;
  if (labelMode != lastLabelMode) {
    tft.fillRect(1, ROW2_LABEL_Y - 3, ROW2_EDGE[3] - 2, 22, BG);
    lastLabelMode = labelMode;
  }

  drawCellLabel(showPartial ? "Avg Speed" : "Max Speed", cellMaxX, ROW2_LABEL_Y);  if (maxPadded != lastMaxStr) {
    if (maxPadded.length() < lastMaxStr.length())
      tft.fillRect(ROW2_EDGE[0] + 3, ROW2_LABEL_Y + 15, ROW2_EDGE[1] - ROW2_EDGE[0] - 6, 60, BG);
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(maxPadded, cellMaxX, ROW2_VALUE_Y,
                   fitValueFont(maxPadded, ROW2_EDGE[1] - ROW2_EDGE[0] - 4));
    lastMaxStr = maxPadded;
  }

  // Cell 2: session time (since boot) or the partial's clock (since 3L).
  // One font-6 string, real colon: the cell is 136px and "12:34" is 123px.
  const unsigned long sesSec =
      (showPartial ? statsPartialMs() : statsSessionMs()) / 1000UL;
  char sesBuf[8];
  snprintf(sesBuf, sizeof(sesBuf), "%02lu:%02lu",
           (sesSec / 60UL) % 100UL, sesSec % 60UL);
  String sesTxt = String(sesBuf);
  drawCellLabel(showPartial ? "Partial time" : "Session time", cellSesX, ROW2_LABEL_Y);
  if (sesTxt != lastSesStr) {
    if (sesTxt.length() < lastSesStr.length())
      tft.fillRect(ROW2_EDGE[1] + 3, ROW2_LABEL_Y + 15,
                   ROW2_EDGE[2] - ROW2_EDGE[1] - 6, 60, BG);
    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(sesTxt, cellSesX, ROW2_VALUE_Y,
                   fitValueFont(sesTxt, ROW2_EDGE[2] - ROW2_EDGE[1] - 4));
    lastSesStr = sesTxt;
  }

  // Ring cell (col 3, rows 1-2): N-up ring, top-aligned. Boat = green
  // triangle and wind = same-size red triangle; BOTH tips are tangent to
  // the ring border and share the same radial span, so they can overlap at
  // close angles. The center shows the
  // boat/wind angle: 0 when the boat points directly into the wind source
  // and 180 when it sails dead downwind. The 60° no-go arc (3px, dark red) hugs the ring opposite the wind
  // triangle — where the wind comes FROM. / View mode (`2S`, three states):
  // north-up = true north at the top; heading-up = the boat at the top;
  // wind-up = the wind SOURCE at the top (no-go arc across 12 o'clock). The
  // wind arrow, the arc and the N glyph all shift by the same rotation, so the
  // picture is identical in every view — only the reference at the top changes.
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

  const int brgDeg = (fixFresh(gps) && gps.course.isValid())
                         ? (int)(gps.course.deg() + 0.5) % 360
                         : -1;
  const bool sesWind = raceSession.valid && raceSession.windSpeed > 0;
  // Env wind is valid once seen — even a calm 0kn report paints the arrow.
  // (Session wind keeps the >0 gate: frozen 0 there means unknown.)
  const bool envWind = raceEnvWindSeen();
  const bool wndValid = sesWind || envWind;
  const int wndKn = sesWind ? raceSession.windSpeed : raceSession.envWindSpeed;
  const int wndAbs = wndValid ? ((sesWind ? raceSession.windDir : raceSession.envWindDir) + 180) % 360 : -1;
  // Upwind direction (where the wind comes FROM) — no-go arc center,
  // exactly opposite the downwind tip of the red wind triangle. The arc
  // width (total degrees) is the /nogo setting.
  const int noGoHalf = noGoArcDeg / 2;
  const int upwRaw = sesWind ? raceSession.windDir
                   : (envWind ? raceSession.envWindDir : -1);
  // ONE rotation for the whole dial. Every element below is its absolute
  // bearing minus `rotDeg`, so a new view is a new offset, not a new code path:
  // north-up 0, heading-up the boat's COG, wind-up the wind SOURCE (which puts
  // the no-go arc at 12 o'clock and the red wind triangle at 6). A view whose
  // input is missing draws as north-up (rot 0) for as long as it is missing.
  ringCanHeading = brgDeg >= 0;
  ringCanWind = upwRaw >= 0;
  int rotDeg = 0;
  int effView = 0;   // the view that is actually drawn (see the tag below)
  if (ringView == 1 && ringCanHeading) { rotDeg = brgDeg; effView = 1; }
  else if (ringView == 2 && ringCanWind) { rotDeg = upwRaw; effView = 2; }
  ringDrawn = effView;
  const int arcDeg = upwRaw >= 0 ? (upwRaw - rotDeg + 360) % 360 : -1;
  const int boatDeg = brgDeg >= 0 ? (brgDeg - rotDeg + 360) % 360 : -1;
  const int windRel = wndAbs >= 0 ? (wndAbs - rotDeg + 360) % 360 : -1;
  const int nDeg = (360 - rotDeg) % 360;

  // View tag, top-left of the cell (the one patch the inscribed ring leaves
  // free). It names the view that is ACTUALLY drawn, not the selected mode: a
  // degraded view (no COG, no wind) reads "North" honestly and flips back by
  // itself when the input returns - labelling a north-up dial "Wind" would be
  // worse than saying nothing. Five chars wide so the next word covers the old
  // one with its own opaque background.
  {
    static const char* viewNames[3] = { "North", "Head", "Wind" };
    String tag = viewNames[effView];
    while (tag.length() < 5) tag += ' ';
    if (tag != lastViewStr) {
      tft.setTextColor(GRAY, BG);
      tft.setTextDatum(TL_DATUM);
      tft.drawString(tag, SPEED_W + 3, BODY_TOP + 2, 2);
      lastViewStr = tag;
    }
  }
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

  // Everything below is change-detected PER ELEMENT. The old version re-ran the
  // whole stack (erase all shapes, redraw the ring, the arc, both triangles,
  // the text) whenever ANY one of them moved — a course update at 1 Hz
  // therefore repainted the no-go band and the ring every second, which is what
  // flickered. Now:
  //   * a triangle is erased/redrawn only if its own angle changed;
  //   * the no-go band only if its centre/size changed;
  //   * the ring circle only when a shape cut it (shapes are tangent to it);
  //   * the ticks + N only when the view rotates (nDeg), because the N now sits
  //     inboard of the band and no shape ever touches it.
  const bool boatMoved = (boatDeg != prevBoatDeg);
  const bool windMoved = (windRel != prevWindDeg);
  const bool arcMoved = (arcDeg != prevArcDeg) || (noGoHalf != prevNoGoHalf);
  const bool ringMoved = (nDeg != prevNDeg);
  const bool textMoved = (cenTxt != lastCenStr);

  if (boatMoved || windMoved || arcMoved || ringMoved || textMoved) {
    // DAMAGE PROPAGATION: a shape is erased by painting its own footprint in
    // the background, which also wipes whatever shared those pixels. Two
    // triangles overlap whenever their bearings are less than 2 x the base's
    // angular half-width apart (~30 deg), and the band (r-12..r) overlaps the
    // outer third of any triangle in its sector. So each moved element drags
    // its neighbours back in, in both directions, and the band is a neighbour
    // of both.
    bool redrawBoat = boatMoved;
    bool redrawWind = windMoved;
    bool redrawArc = arcMoved;
    const int pairDeg = 2 * TRI_HALF_DEG;
    const int bandDeg = noGoHalf + TRI_HALF_DEG;

    if (redrawBoat) {
      if (angleWithin(prevBoatDeg, windRel, pairDeg) ||
          angleWithin(boatDeg, windRel, pairDeg)) redrawWind = true;
      if (arcDeg >= 0 &&
          (angleWithin(prevBoatDeg, arcDeg, bandDeg) ||
           angleWithin(boatDeg, arcDeg, bandDeg))) redrawArc = true;
    }
    if (redrawWind) {
      if (angleWithin(prevWindDeg, boatDeg, pairDeg) ||
          angleWithin(windRel, boatDeg, pairDeg)) redrawBoat = true;
      if (arcDeg >= 0 &&
          (angleWithin(prevWindDeg, arcDeg, bandDeg) ||
           angleWithin(windRel, arcDeg, bandDeg))) redrawArc = true;
    }
    if (redrawArc && arcDeg >= 0) {
      if (angleWithin(windRel, arcDeg, bandDeg) ||
          angleWithin(prevWindDeg, arcDeg, bandDeg)) redrawWind = true;
      if (angleWithin(boatDeg, arcDeg, bandDeg) ||
          angleWithin(prevBoatDeg, arcDeg, bandDeg)) redrawBoat = true;
    }

    // Erase in reverse z-order: shapes first, then whatever is under them.
    if (boatMoved && prevBoatDeg >= 0)
      drawNupTriangle(cmpCx, cmpCy, boatTip, boatBase, prevBoatDeg, BG);
    if (windMoved && prevWindDeg >= 0)
      drawNupTriangle(cmpCx, cmpCy, windTip, windBase, prevWindDeg, BG);
    if (arcMoved && prevArcDeg >= 0)
      drawNoGoArc(cmpCx, cmpCy, cmpR, prevArcDeg, prevNoGoHalf, BG);

    // A triangle's tip is tangent to the ring, so erasing one shaves the border:
    // restore the 1px circle (not the ticks, not the N).
    if (boatMoved || windMoved || arcMoved || redrawBoat || redrawWind) {
      tft.drawCircle(cmpCx, cmpCy, cmpR, RING_GRAY);
    }
    if (ringMoved) {
      if (prevNDeg >= 0) {   // stale N from an old angle
        const double pa = prevNDeg * M_PI / 180.0;
        drawCompassN(cmpCx + (int)((cmpR - RING_BAND - 12) * sin(pa)),
                     cmpCy - (int)((cmpR - RING_BAND - 12) * cos(pa)), " N ", BG);
      }
      drawCompassRing(cmpCx, cmpCy, cmpR, nDeg);
      // The ring's 30-deg ticks reach inboard to r-9, i.e. INSIDE the band's
      // r-12..r thickness, and they are drawn after it: a view rotation would
      // otherwise stripe the band (and clip the triangle tips). Anything that
      // shares the ring's edge has to be repainted on top.
      if (arcDeg >= 0) redrawArc = true;
      if (windRel >= 0) redrawWind = true;
      if (boatDeg >= 0) redrawBoat = true;
    }

    // The N glyph sits at r-RING_BAND-12 = 62px, which is INSIDE a triangle's
    // radial span (base at r-30 = 56 .. tip at r): a triangle parked on the
    // same bearing covers it, and when it moves away the N must come back.
    // Only a ring rotation marks N dirty otherwise, so repair it here — before
    // the shapes, which are then drawn on top as usual.
    if (redrawBoat || redrawWind) {
      if (angleWithin(boatDeg, nDeg, pairDeg) ||
          angleWithin(prevBoatDeg, nDeg, pairDeg) ||
          angleWithin(windRel, nDeg, pairDeg) ||
          angleWithin(prevWindDeg, nDeg, pairDeg)) {
        const double na = nDeg * M_PI / 180.0;
        drawCompassN(cmpCx + (int)((cmpR - RING_BAND - 12) * sin(na)),
                     cmpCy - (int)((cmpR - RING_BAND - 12) * cos(na)), "N", RING_GRAY);
      }
    }

    // Repaint bottom-up: band, then wind, then boat, so the z-order can never
    // invert (the boat is always on top).
    if (redrawArc && arcDeg >= 0)
      drawNoGoArc(cmpCx, cmpCy, cmpR, arcDeg, noGoHalf, ARC_RED);
    if (redrawWind && windRel >= 0)
      drawNupTriangle(cmpCx, cmpCy, windTip, windBase, windRel, RED);
    if (redrawBoat && boatDeg >= 0)
      drawNupTriangle(cmpCx, cmpCy, boatTip, boatBase, boatDeg, GREEN);

    if (textMoved) {
      // Wind angle, centre of the ring. Font 6 (48px) is ~95 px wide — wider
      // than a small dial outright — so the column eats the ring (o172 -> o124)
      // the number drops to font 4 and its footprint with it. The degree mark is
      // part of the block, so the PAIR is centred on the ring's centre: centring
      // just the digits pushed the visual mass ~4px right.
      const uint8_t cenFont = (cmpR >= 80) ? 6 : 4;
      const int cenHalfW = (cmpR >= 80) ? 48 : 26;   // wipe / 2, x
      const int cenHalfH = (cmpR >= 80) ? 24 : 14;   // wipe / 2, y
      const int cenDotY = (cmpR >= 80) ? -18 : -9;   // degree dot height
      const int cenShift = (cmpR >= 80) ? 3 : 2;     // baseline nudge
      const int tw = tft.textWidth(cenTxt, cenFont);
      const int dotD = 6;      // drawn with radius 3
      const int gap = 3;
      const int total = tw + gap + dotD;
      // Wipe only the text's own footprint (+-48 x, +-24 y at font 6). A wider
      // box reached past +-56, where a triangle's BASE sits (r-30), so a wind
      // arrow at 3 or 9 o'clock had its inner edge bitten off by the clear —
      // and in a small dial the same box would eat the no-go band.
      tft.fillRect(cmpCx - cenHalfW, cmpCy - cenHalfH, cenHalfW * 2, cenHalfH * 2, BG);
      tft.setTextColor(WHITE, BG);
      tft.setTextDatum(MC_DATUM);
      tft.drawString(cenTxt, cmpCx - total / 2 + tw / 2, cmpCy + cenShift, cenFont);
      tft.drawCircle(cmpCx - total / 2 + tw + gap + dotD / 2, cmpCy + cenDotY, 3,
                     windAngleDeg >= 0 ? WHITE : BG);
      lastCenStr = cenTxt;
    }

    prevBoatDeg = boatDeg;
    prevWindDeg = windRel;
    prevArcDeg = arcDeg;
    prevNoGoHalf = noGoHalf;
    prevNDeg = nDeg;
  }

  // Row 2, cells 3 and 4: wind speed and bearing. They used to be stacked
  // under the ring; the wider panel gives them their own cells, which also
  // gives the ring the full square.
  const int cellWndX = ROW2_CX[2];                  // 305
  const int cellBrgX = ROW2_CX[3];                  // 420
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
                   fitValueFont(wndTxt, ROW2_EDGE[3] - ROW2_EDGE[2] - 4));
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
    const uint8_t brgFont = fitValueFont(degTxt, ROW2_EDGE[4] - ROW2_EDGE[3] - 26); // room for the ring
    tft.drawString(degTxt, cellBrgX - 6, ROW2_VALUE_Y, brgFont);
    // Degree mark sits at the APEX of the digits, not mid-height: font 6 is
    // 48px tall (half 24), font 4 is 26 (half 13).
    const int glyphHalf = (brgFont == 6) ? 24 : 13;
    tft.drawCircle(cellBrgX - 6 + tft.textWidth(degTxt, brgFont) / 2 + 7,
                   ROW2_VALUE_Y - glyphHalf + 4, 3, brgDeg >= 0 ? WHITE : BG);
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
  lastViewStr = "";   // the page was cleared: repaint the view tag
  lastSpdStr = "";
  lastSpdFont = 8;
  lastSpdW = -1;
  lastSpdH = -1;
  segPrevValid = false;   // the page was cleared: repaint every segment
  prevBoatDeg = -2;
  prevWindDeg = -2;
  prevArcDeg = -1;
  prevNoGoHalf = -1;
  prevNDeg = -1;
}

void drawScreenSpeed(TinyGPSPlus &gps, bool requiresInit)
{
  if (requiresInit) initScreen();

  // Normal display
  drawTopBar(gps);
  drawSpeed(gps);

  drawMainGrid();
  drawHints();

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

    // Right short: cycle the ring view north-up -> heading-up -> wind-up.
    // A view that cannot be drawn right now is skipped (no COG / no wind), so a
    // press always changes something; north-up is always available and closes
    // the loop. RAM only, and a data gap degrades the drawing without losing
    // the chosen mode (see ringView).
    if (button == Button::Right && event == ButtonEvent::ShortPress) {
      if (ringView == 0) ringView = ringCanHeading ? 1 : (ringCanWind ? 2 : 0);
      else if (ringView == 1) ringView = ringCanWind ? 2 : 0;
      else ringView = 0;
      return;
    }

// Right long: jump to the diagnostics screen (2L-only, not part of
    // the L-short cycle)
    if (button == Button::Right && event == ButtonEvent::LongPress) {
      setCurrentPage(PageDiagnostics);
      return;
    }

    // 3S: toggle the row-2 readouts — max/session <-> avg/partial.
    if (button == Button::Third && event == ButtonEvent::ShortPress) {
      showPartial = !showPartial;
      statsForceRedraw();
      return;
    }

    // 3L: start a new partial (sub-session) — its own max/average/clock, the
    // session totals untouched. If the readouts are still showing max/session,
    // flip them to the partial view too: resetting a clock you cannot see is a
    // gesture with no feedback.
    if (button == Button::Third && event == ButtonEvent::LongPress) {
      partialReset();
      showPartial = true;
      statsForceRedraw();
      return;
    }
  }

// ---- ring view, for /status ------------------------------------------------
// Two separate answers on purpose: `mode` is what `2S` selected, `drawn` is
// the view the dial is actually showing. They differ exactly while the mode's
// input is missing (no COG / no wind) - the case a photo cannot describe,
// because the picture then looks like plain north-up.
const char *ringViewModeName() {
  static const char *names[3] = { "north", "head", "wind" };
  return names[(ringView < 0 || ringView > 2) ? 0 : ringView];
}

const char *ringViewDrawnName() {
  static const char *names[3] = { "north", "head", "wind" };
  return names[(ringDrawn < 0 || ringDrawn > 2) ? 0 : ringDrawn];
}

// The availability the button uses to skip a view it cannot draw.
bool ringViewCanHeading() { return ringCanHeading; }
bool ringViewCanWind() { return ringCanWind; }

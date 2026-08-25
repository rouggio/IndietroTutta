#include <Arduino.h>
#include <optional>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include "wifi_manager.h"
#include "config_store.h"
#include "backend.h"
#include "buttons.h"
#include "screens.h"

#include "screen_one.h"

extern TFT_eSPI tft;

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
TriState prevFix = TriState::Unknown;
static TinyGPSPlus* screenOneGPS = nullptr;
struct FlaggedMarker {
  double lat;
  double lon;
  unsigned long startedAt;
};

static constexpr int MAX_FLAGGED_MARKERS = 10;
static FlaggedMarker flaggedMarkers[MAX_FLAGGED_MARKERS];
static int flaggedMarkerCount = 0;
// -1 means the normal GPS time/date is shown. Otherwise this is the marker
// whose elapsed timer is currently displayed.
static int displayedMarker = -1;

// Screen 1 interaction modes
enum class ScreenOneMode {
  Normal,
  Menu,
  WaypointList
};
static ScreenOneMode screenOneMode = ScreenOneMode::Normal;
// When true, next draw will perform a full static redraw for the active mode.
static bool screenOneNeedsRedraw = true;

static String twoDigits(unsigned long value)
{
  return value < 10 ? "0" + String(value) : String(value);
}

static String markerElapsed(unsigned long startedAt)
{
  const unsigned long elapsed = (millis() - startedAt) / 1000;
  const unsigned long hours = elapsed / 3600;
  const unsigned long minutes = (elapsed % 3600) / 60;
  const unsigned long seconds = elapsed % 60;

  return twoDigits(hours) + ":" + twoDigits(minutes) + ":" + twoDigits(seconds);
}

static void rememberFlaggedMarker(TinyGPSPlus &gps)
{
  if (flaggedMarkerCount == MAX_FLAGGED_MARKERS)
  {
    for (int i = 1; i < MAX_FLAGGED_MARKERS; i++)
      flaggedMarkers[i - 1] = flaggedMarkers[i];

    flaggedMarkerCount--;
  }

  flaggedMarkers[flaggedMarkerCount++] = {
    gps.location.lat(),
    gps.location.lng(),
    millis()
  };

  // When a new marker is remembered, show it
  displayedMarker = flaggedMarkerCount - 1;
  screenOneNeedsRedraw = true; // refresh UI minimally on next draw
}

static void rotateDisplayedMarker()
{
  if (flaggedMarkerCount == 0)
  {
    displayedMarker = -1;
    return;
  }

  if (displayedMarker < 0)
    displayedMarker = flaggedMarkerCount - 1;
  else if (displayedMarker == 0)
    displayedMarker = -1;
  else
    displayedMarker--;
}

static void deleteDisplayedMarker()
{
  if (displayedMarker < 0 || displayedMarker >= flaggedMarkerCount)
    return;

  // Shift later markers down to overwrite the deleted one
  for (int i = displayedMarker + 1; i < flaggedMarkerCount; i++) {
    flaggedMarkers[i - 1] = flaggedMarkers[i];
  }

  flaggedMarkerCount--;

  if (flaggedMarkerCount == 0) {
    displayedMarker = -1;
  } else if (displayedMarker >= flaggedMarkerCount) {
    displayedMarker = flaggedMarkerCount - 1;
  }

  screenOneNeedsRedraw = true; // update list view/headers
}

static int daysInMonth(int month, int year)
{
  switch (month)
  {
    case 2:
      return ((year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28);
    case 4:
    case 6:
    case 9:
    case 11:
      return 30;
    default:
      return 31;
  }
}

static void applyTimezoneOffset(int offsetHours, int &hour, int &day, int &month, int &year)
{
  hour += offsetHours;

  while (hour < 0)
  {
    hour += 24;
    day--;
  }

  while (hour >= 24)
  {
    hour -= 24;
    day++;
  }

  while (day < 1)
  {
    month--;
    if (month < 1)
    {
      month = 12;
      year--;
    }
    day = daysInMonth(month, year);
  }

  while (day > daysInMonth(month, year))
  {
    day -= daysInMonth(month, year);
    month++;
    if (month > 12)
    {
      month = 1;
      year++;
    }
  }
}

static String formatTimeWithOffset(TinyGPSPlus &gps, int offsetHours)
{
  if (!gps.time.isValid())
  {
    return "--:--:--";
  }

  int hour = gps.time.hour();
  int minute = gps.time.minute();
  int second = gps.time.second();

  if (gps.date.isValid())
  {
    int day = gps.date.day();
    int month = gps.date.month();
    int year = gps.date.year();
    applyTimezoneOffset(offsetHours, hour, day, month, year);
  }

  return String(hour < 10 ? "0" : "") + String(hour) + ":" + String(minute < 10 ? "0" : "") + String(minute) + ":" + String(second < 10 ? "0" : "") + String(second);
}

static String formatDateWithOffset(TinyGPSPlus &gps, int offsetHours)
{
  if (!gps.date.isValid())
  {
    return "--/--/----";
  }

  int day = gps.date.day();
  int month = gps.date.month();
  int year = gps.date.year();
  int hour = gps.time.isValid() ? gps.time.hour() : 0;

  if (gps.time.isValid())
  {
    applyTimezoneOffset(offsetHours, hour, day, month, year);
  }

  return String(day < 10 ? "0" : "") + String(day) + "/" + String(month < 10 ? "0" : "") + String(month) + "/" + String(year);
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
    tft.pushImage(icon_x + icont_offset_x, 5, icon_width, icon_height, icon_wifi, TFT_BLACK);
    prevWifiConnected = TriState::True;
  }
  else if (!wifiConnected() && prevWifiConnected != TriState::False)
  {
    tft.fillRoundRect(icon_x, 2, tile_width, tile_height, 4, DARK_RED);
    tft.drawRoundRect(icon_x, 2, tile_width, tile_height, 4, TFT_RED);
    tft.setTextColor(TFT_RED, DARK_RED);
    tft.pushImage(icon_x + icont_offset_x, 5, icon_width, icon_height, icon_no_signal, TFT_BLACK);
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

  // Fix Icon
  icon_x += tile_width + tile_spacing;
  if (gps.location.isValid() && prevFix != TriState::True)
  {
    tft.fillRoundRect(icon_x, 2, tile_width, tile_height, 4, TFT_DARKGREEN);
    tft.drawRoundRect(icon_x, 2, tile_width, tile_height, 4, GREEN);
    tft.setTextColor(WHITE, TFT_DARKGREEN);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("FX", icon_x + icont_offset_x, 7, 2);
    prevFix = TriState::True;
  }
  else if (!gps.location.isValid() && prevFix != TriState::False)
  {
    tft.fillRoundRect(icon_x, 2, tile_width, tile_height, 4, DARK_RED);
    tft.drawRoundRect(icon_x, 2, tile_width, tile_height, 4, TFT_RED);
    tft.setTextColor(WHITE, DARK_RED);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("FX", icon_x + icont_offset_x, 6, 2);
    prevFix = TriState::False;
  }


  // Right aligned symbols

  int sats = gps.satellites.isValid() ? gps.satellites.value() : 0;

  // GPS satellites (top-right)
  tft.pushImage(tft.width() - 57, 7, 16, 16, icon_sat, TFT_BLACK);

  tft.setTextColor(sats > 0 ? sats > MIN_SAT_THRESHOLD ? GREEN : TFT_YELLOW : TFT_RED, BG);
  tft.setTextDatum(TR_DATUM);
  String satStr = (sats < 10) ? "0" + String(sats) : String(sats);
  tft.drawString(satStr, tft.width() - 10, 5, 4);


  tft.drawFastHLine(0, 30, tft.width(), GRAY);
}

void drawSpeed(TinyGPSPlus &gps)
{
  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(WHITE, BG);
  tft.drawString("SPEED (kn)", tft.width() / 2, 50, 2);

  tft.setTextColor(TFT_YELLOW, BG);
  if (gps.speed.isValid()) {
    String spd = " " + String(gps.speed.knots(), 1) + " ";
    tft.drawString(spd, tft.width() / 2, 100, 8);
  } else {
    String spd = "  ---  ";
    tft.drawString(spd, tft.width() / 2, 100, 8);
  }
}

void drawCourse(TinyGPSPlus &gps)
{
  String bearing = gps.course.isValid() ? String((int)gps.course.deg()) : "---";
  String dirName = gps.course.isValid() ? "(" + String(TinyGPSPlus::cardinal(gps.course.deg())) + ")" : "";
  String courseString = "       " + bearing + dirName + "       ";

  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(MC_DATUM);

  tft.drawString("COURSE", tft.width() / 2, 155, 2);
  tft.drawString(courseString, tft.width() / 2, 180, 4);
}

void drawBottomBar(String timeStr, String dateStr)
{
  tft.drawFastHLine(20, 200, tft.width() - 40, GRAY);

  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(BC_DATUM);

  String bottom = "  " + timeStr + "  |  " + dateStr + "  ";
  tft.drawString(bottom, tft.width() / 2, 235, 4);
}

void initScreen() {
  tft.fillScreen(TFT_BLACK);
  prevWifiConnected = TriState::Unknown;
  prevDataConnected = TriState::Unknown;
  prevFix = TriState::Unknown;
}

void drawScreenOne(TinyGPSPlus &gps, bool requiresInit)
{
  screenOneGPS = &gps;

  if (requiresInit) initScreen();

  // Global config is loaded once at boot and kept in sync
  // when the portal saves a new configuration
  int timezoneOffsetHours = config.timezoneOffsetHours;

  // Mode: Menu
  if (screenOneMode == ScreenOneMode::Menu) {
    if (screenOneNeedsRedraw) {
      tft.fillScreen(BG);
      drawTopBar(gps);
      tft.setTextColor(WHITE, BG);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("MENU", tft.width() / 2, 50, 4);
      tft.drawString("> Waypoints", tft.width() / 2, 120, 2);

      // Bottom labels: left-aligned LB, right-aligned RLC
      tft.setTextDatum(BC_DATUM);
      tft.setTextColor(WHITE, BG);
      tft.drawString("LB: Exit", 8, 235, 2);
      tft.setTextDatum(BR_DATUM);
      tft.drawString("RLC: List", tft.width() - 8, 235, 2);

      screenOneNeedsRedraw = false;
    }

    // Menu has no rapidly changing content; return quickly.
    return;
  }

  // Mode: Waypoint list
  if (screenOneMode == ScreenOneMode::WaypointList) {
    if (screenOneNeedsRedraw) {
      tft.fillScreen(BG);
      drawTopBar(gps);

      tft.setTextColor(WHITE, BG);
      tft.setTextDatum(MC_DATUM);

      if (flaggedMarkerCount == 0) {
        tft.drawString("No waypoints", tft.width() / 2, 110, 4);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("LB: Back", 8, 235, 2);
        screenOneNeedsRedraw = false;
        return;
      }

      int idx = displayedMarker >= 0 ? displayedMarker : (flaggedMarkerCount - 1);
      if (idx < 0) idx = 0;

      String header = "Waypoint " + String(idx + 1) + "/" + String(flaggedMarkerCount);
      tft.drawString(header, tft.width() / 2, 60, 4);

      tft.setTextDatum(MC_DATUM);
      tft.setTextSize(1);
      String lat = "Lat: " + String(flaggedMarkers[idx].lat, 6);
      String lon = "Lon: " + String(flaggedMarkers[idx].lon, 6);
      tft.drawString(lat, tft.width() / 2, 120, 2);
      tft.drawString(lon, tft.width() / 2, 150, 2);

      // Bottom labels: left LB, right RB/RLB
      tft.setTextDatum(BC_DATUM);
      tft.drawString("LB: Back", 8, 235, 2);
      tft.setTextDatum(BR_DATUM);
      tft.drawString("RB: Next  RLB: Delete", tft.width() - 8, 235, 2);

      // Show elapsed in bottom bar
      drawBottomBar("WP " + String(idx + 1), markerElapsed(flaggedMarkers[idx].startedAt));

      screenOneNeedsRedraw = false;
      return;
    }

    // If not a full redraw, only refresh dynamic bottom bar (elapsed)
    int idx = displayedMarker >= 0 ? displayedMarker : (flaggedMarkerCount - 1);
    if (idx < 0) idx = 0;
    drawBottomBar("WP " + String(idx + 1), markerElapsed(flaggedMarkers[idx].startedAt));
    return;
  }

  // Normal display
  drawTopBar(gps);
  drawSpeed(gps);
  drawCourse(gps);
  if (displayedMarker >= 0 && displayedMarker < flaggedMarkerCount)
  {
    drawBottomBar("MARK " + String(displayedMarker + 1),
                  markerElapsed(flaggedMarkers[displayedMarker].startedAt));
  }
  else
  {
    drawBottomBar(formatTimeWithOffset(gps, timezoneOffsetHours),
                  formatDateWithOffset(gps, timezoneOffsetHours));
  }
}

void screenOneButton(
    Button button,
    ButtonEvent event
) {

    // Left short: next screen, or exit menu
    if (button == Button::Left && event == ButtonEvent::ShortPress) {
        if (screenOneMode == ScreenOneMode::Menu || screenOneMode == ScreenOneMode::WaypointList) {
            screenOneMode = ScreenOneMode::Normal;
            screenOneNeedsRedraw = true;
        } else {
            nextScreen();
        }
        return;
    }

    // Right short: open map screen
    if (button == Button::Right && event == ButtonEvent::ShortPress) {
        setCurrentPage(1); // go to map
        return;
    }

    // Left long: open/close menu when on normal
    if (button == Button::Left && event == ButtonEvent::LongPress) {
        if (screenOneMode == ScreenOneMode::Normal) {
            screenOneMode = ScreenOneMode::Menu;
            screenOneNeedsRedraw = true;
        } else {
            screenOneMode = ScreenOneMode::Normal;
            screenOneNeedsRedraw = true;
        }
        return;
    }

    // Right long: context-sensitive
    if (button == Button::Right && event == ButtonEvent::LongPress) {
        if (screenOneMode == ScreenOneMode::Menu) {
            // From the menu, RLC lists waypoints
            if (flaggedMarkerCount > 0) {
                screenOneMode = ScreenOneMode::WaypointList;
                displayedMarker = flaggedMarkerCount - 1;
                screenOneNeedsRedraw = true;
            } else {
                // nothing to list; return to normal
                screenOneMode = ScreenOneMode::Normal;
                screenOneNeedsRedraw = true;
            }
            return;
        }

        if (screenOneMode == ScreenOneMode::WaypointList) {
            // Long press in list deletes current waypoint
            deleteDisplayedMarker();
            screenOneNeedsRedraw = true;
            return;
        }

        // Normal behavior: send flagged waypoint
        if (screenOneGPS) {
            if (backendSendFlaggedPosition(*screenOneGPS)) {
                rememberFlaggedMarker(*screenOneGPS);
            }
        }
        return;
    }
}

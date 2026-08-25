#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>

#include "backend.h"
#include "screens.h"
#include "screen_waypoints.h"

extern TFT_eSPI tft;

// ==== COLORS ====
#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define GRAY 0x7BEF

static TinyGPSPlus* waypointsGPS = nullptr;

struct FlaggedMarker {
  double lat;
  double lon;
  unsigned long startedAt;
};

static constexpr int MAX_FLAGGED_MARKERS = 10;
static FlaggedMarker flaggedMarkers[MAX_FLAGGED_MARKERS];
static int flaggedMarkerCount = 0;
// Index of the waypoint currently shown; -1 when none selected.
static int displayedMarker = -1;

static bool waypointsNeedsRedraw = true;

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

static void drawBottomBar(String timeStr, String dateStr)
{
  tft.drawFastHLine(20, 200, tft.width() - 40, GRAY);

  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(BC_DATUM);

  String bottom = "  " + timeStr + "  |  " + dateStr + "  ";
  tft.drawString(bottom, tft.width() / 2, 235, 4);
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
  waypointsNeedsRedraw = true;
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

  waypointsNeedsRedraw = true;
}

void drawScreenWaypoints(TinyGPSPlus &gps, bool requiresInit)
{
  waypointsGPS = &gps;

  if (requiresInit) {
    tft.fillScreen(BG);
    waypointsNeedsRedraw = true;
  }

  if (!waypointsNeedsRedraw) {
    // Refresh only the elapsed timer of the shown waypoint
    int idx = displayedMarker >= 0 ? displayedMarker : (flaggedMarkerCount - 1);
    if (idx >= 0 && idx < flaggedMarkerCount) {
      drawBottomBar("WP " + String(idx + 1), markerElapsed(flaggedMarkers[idx].startedAt));
    }
    return;
  }

  tft.fillScreen(BG);
  waypointsNeedsRedraw = false;

  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("WAYPOINTS", tft.width() / 2, 25, 4);

  if (flaggedMarkerCount == 0) {
    tft.drawString("No waypoints", tft.width() / 2, 110, 4);
  } else {
    int idx = displayedMarker >= 0 ? displayedMarker : (flaggedMarkerCount - 1);
    if (idx < 0) idx = 0;

    String header = "Waypoint " + String(idx + 1) + "/" + String(flaggedMarkerCount);
    tft.drawString(header, tft.width() / 2, 65, 4);

    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    String lat = "Lat: " + String(flaggedMarkers[idx].lat, 6);
    String lon = "Lon: " + String(flaggedMarkers[idx].lon, 6);
    tft.drawString(lat, tft.width() / 2, 120, 2);
    tft.drawString(lon, tft.width() / 2, 150, 2);
  }

  // Bottom labels
  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(BL_DATUM);
  tft.drawString("L - Next", 8, 235, 2);
  tft.setTextDatum(BR_DATUM);
  tft.drawString("R Cyc  LL Flag  RR Del", tft.width() - 8, 235, 2);

  if (flaggedMarkerCount > 0) {
    int idx = displayedMarker >= 0 ? displayedMarker : (flaggedMarkerCount - 1);
    if (idx < 0) idx = 0;
    drawBottomBar("WP " + String(idx + 1), markerElapsed(flaggedMarkers[idx].startedAt));
  }
}

void screenWaypointsButton(Button button, ButtonEvent event)
{
  // Left short: advance to the next page
  if (button == Button::Left && event == ButtonEvent::ShortPress) {
    nextScreen();
    return;
  }

  // Right short: cycle through waypoints
  if (button == Button::Right && event == ButtonEvent::ShortPress) {
    rotateDisplayedMarker();
    waypointsNeedsRedraw = true;
    return;
  }

  // Left long: flag current position
  if (button == Button::Left && event == ButtonEvent::LongPress) {
    if (waypointsGPS && backendSendFlaggedPosition(*waypointsGPS)) {
      rememberFlaggedMarker(*waypointsGPS);
    }
    return;
  }

  // Right long: delete shown waypoint
  if (button == Button::Right && event == ButtonEvent::LongPress) {
    deleteDisplayedMarker();
    return;
  }
}

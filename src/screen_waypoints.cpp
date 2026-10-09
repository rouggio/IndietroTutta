#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>

#include "backend.h"
#include "screens.h"
#include "screen_waypoints.h"

#include "canvas.h"

// ==== COLORS ====
#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define GRAY 0x7BEF

static TinyGPSPlus* waypointsGPS = nullptr;

struct FlaggedMarker {
  double lat;
  double lon;
  unsigned long startedAt;
  char uid[41]; // backend id for this waypoint, "" if never sent
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
  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(BC_DATUM);

  String bottom = "  " + timeStr + "  |  " + dateStr + "  ";
  // Draw above the hint bar (y=235) so the button hints survive
  tft.drawString(bottom, tft.width() / 2, 208, 4);
}

static unsigned int waypointSeq = 0;

static void makeWaypointUid(char* out, size_t len)
{
  snprintf(out, len, "wp-%lu-%u", (unsigned long)millis(), ++waypointSeq);
}

static void rememberFlaggedMarker(TinyGPSPlus &gps, const char* uid)
{
  if (flaggedMarkerCount == MAX_FLAGGED_MARKERS)
  {
    for (int i = 1; i < MAX_FLAGGED_MARKERS; i++)
      flaggedMarkers[i - 1] = flaggedMarkers[i];

    flaggedMarkerCount--;
  }

  FlaggedMarker m = {
    gps.location.lat(),
    gps.location.lng(),
    millis(),
    {0}
  };

  if (uid) {
    strncpy(m.uid, uid, sizeof(m.uid) - 1);
  }

  flaggedMarkers[flaggedMarkerCount++] = m;

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

  // Sync the delete to the backend (best effort, queued)
  backendEnqueueDeleteWaypoint(flaggedMarkers[displayedMarker].uid);

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
  tft.drawString("WAYPOINTS", tft.width() / 2, 20, 4);
  tft.drawFastHLine(0, 44, tft.width(), GRAY);

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

  // Bottom labels: L/LL on the left, R/RR on the right
  tft.drawFastHLine(0, 214, tft.width(), GRAY);
  tft.setTextColor(GRAY, BG);
  tft.setTextDatum(BL_DATUM);
  tft.drawString("L Next  LL Flag", 8, 235, 2);
  tft.setTextDatum(BR_DATUM);
  tft.drawString("R Cyc  RR Del", tft.width() - 8, 235, 2);

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
    char uid[41];
    makeWaypointUid(uid, sizeof(uid));
    if (waypointsGPS && backendSendFlaggedPosition(*waypointsGPS, uid)) {
      rememberFlaggedMarker(*waypointsGPS, uid);
    }
    return;
  }

  // Right long: delete shown waypoint
  if (button == Button::Right && event == ButtonEvent::LongPress) {
    deleteDisplayedMarker();
    return;
  }
}

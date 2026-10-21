#include "screens.h"
#include "canvas.h"
#include "screen_speed.h"
#include "screen_diagnostics.h"
#include "screen_race.h"
#include "screen_config.h"
#include "splash_screen.h"
#include "buttons.h"
#include "backend.h"
#include "serial_buffer.h"

#include <TFT_eSPI.h>
#include <SPI.h>
#include <string.h>

static UIState uiState = UIState::Screens;

static const int CX = 160;
static const int CY = 120;

TFT_eSPI tftHW = TFT_eSPI();
TFT_eSPI*    gPanel    = &tftHW;   // the hardware panel
TFT_eSprite* gCapture  = nullptr;  // band sprite while a /screen capture runs
int32_t      gCaptureY = 0;        // page y of the band's first row
Canvas       tftCanvas;            // drawing dispatcher: `tft` is this
bool gGrabbing = false;            // true while rendering a /screen capture

unsigned long lastUpdate = 0;

int prevPage = -1;
ScreenPage page = PageMain;

void screenInit() {
  tft.init();
  tft.invertDisplay(false);
  // Try rotation 3 (opposite landscape) and use MADCTL 0x68 (MX|MV|BGR)
  // Use TFT_eSPI's setRotation only; avoid manual MADCTL to prevent conflicts
  tft.setRotation(3);
}

// --------------------------------------------------
// NON-BLOCKING SPLASH
// Drawn once in setup(); screenLoop() and the button
// router keep it on screen while setup runs, and it
// is released by endSplash() as soon as the main loop
// is ready (minimum display time keeps it from being
// a single-frame flash).
// --------------------------------------------------

static constexpr unsigned long SPLASH_MIN_MS = 500;
static unsigned long splashShownAt = 0;
static bool splashDone = false;

void beginSplash() {
  drawSplashScreen();
  splashShownAt = millis();
  splashDone = false;
}

void endSplash() {
  splashDone = true;
}

bool splashActive() {
  return !splashDone &&
         splashShownAt != 0 &&
         (millis() - splashShownAt) < SPLASH_MIN_MS;
}

// ---------------- MAIN ROUTER ----------------
void drawScreen(TinyGPSPlus &gps, bool requiresInit, ScreenPage page) {
  switch (page) {
    case PageMain: drawScreenSpeed(gps, requiresInit); break;
    case PageDiagnostics: drawScreenDiagnostics(gps); break;
    case PageRace: drawScreenRace(gps, requiresInit); break;
    case PageConfig: drawScreenConfig(requiresInit); break;

    default:
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("PAGE ERROR", CX, CY, 4);
      break;
  }
}

// There is deliberately NO entry gate on the race screen (todo 42). It used to
// refuse to open unless /health had succeeded in the last 60s, and it said so by
// filling the top 28 px band with a red "NO NETWORK" strip on every 200 ms pass
// for two seconds, then forcing a full page repaint to erase it - the band the
// page's own header owns, wiped and redrawn five times a second.
//
// The screen never needed the link: the session is cached in NVS (and kept while
// offline), the run upload retries until it lands, and even a practice start
// whose POST /sessions fails keeps a local OFFLINE session on purpose. So the
// device is built to sail offline and only the entry gate disagreed.
// "Nothing to show" is handled by the race page's own empty state, drawn once by
// the draw path that owns those pixels; link staleness is reported by the
// top-bar WiFi/data tiles, which never lie and never strobe.

static const char* pageName(ScreenPage p)
{
    switch (p) {
        case PageMain: return "main";
        case PageDiagnostics: return "diagnostics";
        case PageRace: return "race";
        case PageConfig: return "config";
        default: return "unknown";
    }
}

void nextScreen() {
  // Cycle between the top-level pages. DIAGNOSTICS and CONFIG are
  // reachable only via speed-screen gestures (RR and LL) and are not
  // part of the L-short cycle.
  do {
    page = (ScreenPage)(((int)page + 1) % ((int)PageConfig + 1));
  } while (page == PageDiagnostics || page == PageConfig);
  bufferedSerialPrintln(String("[PAGE] nextScreen -> ") + pageName(page));
  // Leaving the race page drops the finished-race hold (todo 38): the result is
  // for the sailor standing on the line, not a permanent memorial.
  if (page != PageRace) raceFinishHoldClear();
  tft.fillScreen(TFT_BLACK);
}

void setCurrentPage(ScreenPage p) {
    if (p < PageMain || p > PageConfig) return;
    page = p;
    bufferedSerialPrintln(String("[PAGE] setCurrentPage -> ") + pageName(page));
    if (p != PageRace) raceFinishHoldClear(); // same rule as nextScreen (todo 38)
    // Clear immediately: no stale pixels may survive a page
    // switch, even before the next redraw pass.
    prevPage = (p == PageMain) ? PageDiagnostics : PageMain;
    tft.fillScreen(TFT_BLACK);
}

void redrawCurrentPage() {
    tft.fillScreen(TFT_BLACK);
    // Force a full (requiresInit) redraw on the next pass
    prevPage = (page == PageMain) ? PageDiagnostics : PageMain;
}

// --------------------------------------------------
// BUTTON EVENT ROUTER
// --------------------------------------------------

void screenButtonEvent(
    Button button,
    ButtonEvent event
) {
    // Swallow presses while the boot splash is on screen
    if (splashActive()) {
        return;
    }

    switch(page) {
        case PageMain: screenSpeedButton(button, event); break;
        case PageDiagnostics: screenDiagnosticsButton(button, event); break;
        case PageRace: screenRaceButton(button, event); break;
        case PageConfig: screenConfigButton(button, event); break;
    }
}

void screenLoop(TinyGPSPlus &gps) {
    // Hold the splash on screen; everything else keeps running
    if (splashActive()) {
        return;
    }

    if (millis() - lastUpdate > 200) {
        lastUpdate = millis();
        drawScreen(
            gps,
            prevPage != page,
            page
        );
        prevPage = page;
    }
}

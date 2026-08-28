#include "screens.h"
#include "screen_speed.h"
#include "screen_diagnostics.h"
#include "screen_waypoints.h"
#include "screen_timers.h"
#include "screen_config.h"
#include "splash_screen.h"
#include "buttons.h"

#include <TFT_eSPI.h>
#include <SPI.h>

static UIState uiState = UIState::Screens;

static const int CX = 160;
static const int CY = 120;

TFT_eSPI tft = TFT_eSPI();

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
    case PageWaypoints: drawScreenWaypoints(gps, requiresInit); break;
    case PageTimers: drawScreenTimers(requiresInit); break;
    case PageConfig: drawScreenConfig(requiresInit); break;

    default:
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("PAGE ERROR", CX, CY, 4);
      break;
  }
}

void nextScreen() {
  // Cycle between the top-level pages. DIAGNOSTICS and CONFIG are
  // reachable only via speed-screen gestures (RR and LL) and are not
  // part of the L-short cycle.
  do {
    page = (ScreenPage)(((int)page + 1) % ((int)PageConfig + 1));
  } while (page == PageDiagnostics || page == PageConfig);
  tft.fillScreen(TFT_BLACK);
}

void setCurrentPage(ScreenPage p) {
    if (p < PageMain || p > PageConfig) return;
    page = p;
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
        case PageWaypoints: screenWaypointsButton(button, event); break;
        case PageTimers: screenTimersButton(button, event); break;
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

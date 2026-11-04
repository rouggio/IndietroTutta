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

// (the old `UIState uiState` switch stood here; nothing ever read it - the page
// router is `page`/`screenLoop`, and every overlay owns its own flag.)

// (Full-screen messages centre themselves on tft.width()/height(); the old
// 320x240 CX/CY constants are gone with that panel.)

TFT_eSPI tftHW = TFT_eSPI();
TFT_eSPI*    gPanel    = &tftHW;   // the hardware panel
TFT_eSprite* gCapture  = nullptr;  // band sprite while a /screen capture runs
int32_t      gCaptureY = 0;        // page y of the band's first row
Canvas       tftCanvas;            // drawing dispatcher: `tft` is this
bool gGrabbing = false;            // true while rendering a /screen capture

unsigned long lastUpdate = 0;

int prevPage = -1;
ScreenPage page = PageMain;

// Brand navy (#0f172a) — the splash screen background and the title bar of the
// utility pages (UPDATE, CONFIG, DIAGNOSTICS). Defined once so they cannot
// drift apart; callers pass it as the text background too.
uint16_t brandNavy() {
  return tft.color565(15, 23, 42);
}

void screenInit() {
  tft.init();
  tft.invertDisplay(false);
  // Rotation is the panel's: TFT_ROTATION comes from include/User_Setup.h
  // (1 = landscape, this ST7796S). Fallback keeps a bare build working.
  // TFT_eSPI's setRotation only; avoid manual MADCTL to prevent conflicts.
#ifndef TFT_ROTATION
#define TFT_ROTATION 3
#endif
  tft.setRotation(TFT_ROTATION);
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
    case PageDiagnostics: drawScreenDiagnostics(gps, requiresInit); break;
    case PageRace: drawScreenRace(gps, requiresInit); break;
    case PageConfig: drawScreenConfig(requiresInit); break;

    default:
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("PAGE ERROR", tft.width() / 2, tft.height() / 2, 4);
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
        // Entering the race screen is a request for freshness (todo 68): from
        // here on the health task runs the engaged cadence, but its next tick
        // could be up to 5 s away. Force one now, so the empty race screen is
        // answered at once. Covers every way in - setCurrentPage() (a session
        // arrived, or the menu), nextScreen() (the L cycle) - because it keys
        // on the transition, not on the caller. backendPollHealthNow() only
        // raises a flag, so it is safe from the UI loop.
        if (prevPage != page && page == PageRace) {
            backendPollHealthNow();
        }
        drawScreen(
            gps,
            prevPage != page,
            page
        );
        prevPage = page;
    }
}

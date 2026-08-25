#include "screens.h"
#include "screen_one.h"
#include "screen_map.h"
#include "screen_two.h"
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
int page = 0;

const int NUM_PAGES = 3;

void screenInit() {
  tft.init();
  tft.invertDisplay(false);
  // Try rotation 3 (opposite landscape) and use MADCTL 0x68 (MX|MV|BGR)
  // Use TFT_eSPI's setRotation only; avoid manual MADCTL to prevent conflicts
  tft.setRotation(3);
}

void drawSplash() {
  drawSplashScreen();
}

// ---------------- MAIN ROUTER ----------------
void drawScreen(TinyGPSPlus &gps, bool requiresInit, int page) {
  switch (page) {
    case 0: drawScreenOne(gps, requiresInit); break;
    case 1: drawScreenMap(gps, requiresInit); break;
    case 2: drawScreenTwo(gps); break;

    default:
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("PAGE ERROR", CX, CY, 4);
      break;
  }
}

void nextScreen() {
    // Advance to the next page, skipping the map page (index 1):
    // the map stays reachable via its dedicated Right button.
    // 0 -> 2, 2 -> 0, 1 -> 2
    page = (page + 1) % NUM_PAGES;

    if (page == 1) {
        page = 2;
    }

    tft.fillScreen(TFT_BLACK);
}

void setCurrentPage(int p) {
    if (p < 0 || p >= NUM_PAGES) return;
    page = p;
    // force a redraw on next loop
    prevPage = -1;
    tft.fillScreen(TFT_BLACK);
}

// --------------------------------------------------
// BUTTON EVENT ROUTER
// --------------------------------------------------

void screenButtonEvent(
    Button button,
    ButtonEvent event
) {
    switch(page) {
        case 0: screenOneButton(button, event); break;
        case 1: screenMapButton(button, event); break;
        case 2: screenTwoButton(button, event); break;
    }
}

void screenLoop(TinyGPSPlus &gps) {
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

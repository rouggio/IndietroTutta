#include "screens.h"
#include "screen_one.h"
#include "screen_two.h"
#include "screen_three.h"
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
  tft.invertDisplay(true);
  tft.setRotation(3);
  tft.writecommand(0x36);
  tft.writedata(0x68);
}

void drawSplash() {
  drawSplashScreen();
}

// ---------------- MAIN ROUTER ----------------
void drawScreen(TinyGPSPlus &gps, bool requiresInit, int page) {
  switch (page) {
    case 0: drawScreenOne(gps, requiresInit); break;
    case 1: drawScreenTwo(gps); break;
    case 2: drawScreenThree(gps); break;

    default:
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("PAGE ERROR", CX, CY, 4);
      break;
  }
}

void nextScreen() {
    page = (page + 1) % NUM_PAGES;
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
        case 1: screenTwoButton(button, event); break;
        case 2: screenThreeButton(button, event); break;
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
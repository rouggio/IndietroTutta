#include "screens.h"
#include "screen_one.h"
#include "screen_two.h"
#include "splash_screen.h"

#include <TFT_eSPI.h>
#include <SPI.h>

static const int CX = 160;
static const int CY = 120;

const int SCREEN_BUTTON_PIN = 22;
const unsigned long BUTTON_DEBOUNCE_MS = 250;

TFT_eSPI tft = TFT_eSPI();

unsigned long lastButtonChange = 0;
unsigned long lastUpdate = 0;

int page = 0;
const int NUM_PAGES = 2;
bool lastButtonState = HIGH;

void screenInit() {
  tft.init();
  tft.invertDisplay(false);
  tft.setRotation(1);
  tft.writecommand(0x36);
  tft.writedata(0x68);

  pinMode(SCREEN_BUTTON_PIN, INPUT_PULLUP);
}

void drawSplash() {
  drawSplashScreen();
}

// ---------------- MAIN ROUTER ----------------
void drawScreen(TinyGPSPlus &gps, int page) {
  switch (page) {
    case 0: drawScreenOne(gps); break;
    case 1: drawScreenTwo(gps); break;

    default:
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("PAGE ERROR", CX, CY, 4);
      break;
  }
  
}

void screenLoop(TinyGPSPlus &gps) {
  int buttonState = digitalRead(SCREEN_BUTTON_PIN);
  if (buttonState == LOW && lastButtonState == HIGH && millis() - lastButtonChange > BUTTON_DEBOUNCE_MS) {
    lastButtonChange = millis();
    page = (page + 1) % NUM_PAGES;
    tft.fillScreen(TFT_BLACK);
  }
  lastButtonState = buttonState;

  // refresh display
  if (millis() - lastUpdate > 200) {
    lastUpdate = millis();
    drawScreen(gps, page);
  }
}
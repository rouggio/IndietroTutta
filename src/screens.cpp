#include "screens.h"

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
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Indietro Tutta!", CX, CY, 4);
  delay(2000);
  tft.fillScreen(TFT_BLACK);
}

static void maybeClear(int newState) {
  static int lastState = -1;
  if (newState != lastState) {
    lastState = newState;
    tft.fillScreen(TFT_BLACK);
  }
}

// ---------------- SCREEN ONE ----------------
static void drawScreenOne(TinyGPSPlus &gps) {
  float speed = gps.speed.isValid() ? gps.speed.knots() : 0;
  int satCount = gps.satellites.isValid() ? gps.satellites.value() : 0;


  if (gps.speed.isValid()) {
    maybeClear(1);

    // number of connected satellites
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);      // Label: top-left
    tft.drawString("SAT", 270, 4, 2);
    tft.setTextDatum(TR_DATUM);      // Value: top-right
    char buf[4];
    sprintf(buf, "%3d", satCount);
    tft.drawString(buf, 315, 4, 2);

    // speed in knots
    float speed = gps.speed.knots();
    tft.setTextDatum(MC_DATUM);
    // todo this fon has no whitespace, hence clearing won't work, need to clear the area manually
    tft.setTextColor(TFT_BLACK, TFT_BLACK);
    tft.drawString("      ", CX, CY - 20, 8);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(String(speed, 1), CX, CY - 20, 8);
    tft.drawString("KNOTS", CX, CY + 40, 4);

  } else {

    maybeClear(2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Waiting GPS Fix", CX, CY, 4);

  }
}

// ---------------- SCREEN TWO ----------------
static void drawScreenTwo(TinyGPSPlus &gps) {
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  if (gps.location.isValid()) {
    maybeClear(1);
    tft.drawString("LAT: " + String(gps.location.lat(), 6), 10, 10, 4);
    tft.drawString("LON: " + String(gps.location.lng(), 6), 10, 40, 4);
    tft.drawString("Bearing: " + String(gps.course.deg(), 0), 10, 70, 4);
    tft.drawString("SAT: " + String(gps.satellites.value()), 10, 100, 4);
    tft.drawString("HDOP: " + String(gps.hdop.hdop(), 1), 10, 130, 4);
    tft.drawString("ALT: " + String(gps.altitude.meters(), 0) + " m", 10, 160, 4);
  } else {
    maybeClear(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Waiting GPS Fix", CX, CY, 4);
  }
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
#include "screens.h"

static const int CX = 160;
static const int CY = 120;

void drawSplash(TFT_eSPI &tft) {
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLUE, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("GPS MARINE", CX, CY, 4);
  delay(2000);
  tft.fillScreen(TFT_BLACK);
}

static void maybeClear(TFT_eSPI &tft, int newState) {
  static int lastState = -1;
  if (newState != lastState) {
    lastState = newState;
    tft.fillScreen(TFT_BLACK);
  }
}

// ---------------- SCREEN ONE ----------------
static void drawScreenOne(TFT_eSPI &tft, TinyGPSPlus &gps) {
  float speed = gps.speed.isValid() ? gps.speed.knots() : 0;
  int satCount = gps.satellites.isValid() ? gps.satellites.value() : 0;


  if (gps.speed.isValid() && gps.satellites.isValid()) {
    maybeClear(tft, 0);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Sats: " + String(satCount), 315, 4, 2);

    float speed = gps.speed.knots();
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(String(speed, 1), CX, CY - 20, 8);
    tft.drawString("KNOTS", CX, CY + 40, 4);
  } else {
    maybeClear(tft, 1);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("NO GPS", CX, CY, 4);
  }
}

// ---------------- COURSE ----------------
static void drawCourse(TFT_eSPI &tft, TinyGPSPlus &gps) {
  tft.setTextDatum(MC_DATUM);

  if (gps.course.isValid()) {
    maybeClear(tft, 2);
    float c = gps.course.deg();
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("COURSE", CX, CY - 60, 4);
    tft.drawString(String(c, 0) + "°", CX, CY, 6);
  } else {
    maybeClear(tft, 3);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("NO COURSE DATA", CX, CY, 4);
  }
}

// ---------------- POSITION ----------------
static void drawPosition(TFT_eSPI &tft, TinyGPSPlus &gps) {
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  if (gps.location.isValid()) {
    maybeClear(tft, 4);
    tft.drawString("LAT:", 10, 30, 4);
    tft.drawString(String(gps.location.lat(), 6), 10, 70, 4);
    tft.drawString("LON:", 10, 120, 4);
    tft.drawString(String(gps.location.lng(), 6), 10, 160, 4);
  } else {
    maybeClear(tft, 5);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("WAITING GPS FIX", CX, CY, 4);
  }
}

// ---------------- STATUS ----------------
static void drawStatus(TFT_eSPI &tft, TinyGPSPlus &gps) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("SAT: " + String(gps.satellites.value()), 10, 40, 4);
  tft.drawString("HDOP: " + String(gps.hdop.hdop(), 1), 10, 90, 4);
  tft.drawString("ALT: " + String(gps.altitude.meters(), 0) + " m", 10, 140, 4);
}

// ---------------- MAIN ROUTER ----------------
void drawScreen(TFT_eSPI &tft, TinyGPSPlus &gps, int page) {
  switch (page) {
    case 0: drawScreenOne(tft, gps); break;
    case 1: drawCourse(tft, gps); break;
    case 2: drawPosition(tft, gps); break;
    case 3: drawStatus(tft, gps); break;

    default:
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("PAGE ERROR", CX, CY, 4);
      break;
  }
  
}

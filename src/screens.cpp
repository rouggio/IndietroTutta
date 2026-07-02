#include "screens.h"

void drawSplash(TFT_eSPI &tft)
{
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("GPS MARINE", 120, 140, 4);
}

static void drawSpeed(TFT_eSPI &tft, TinyGPSPlus &gps)
{
  float speed = gps.speed.isValid() ? gps.speed.knots() : 0;

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  tft.drawString(String(speed, 1), 120, 120, 7);
  tft.drawString("KNOTS", 120, 200, 4);
}

static void drawCourse(TFT_eSPI &tft, TinyGPSPlus &gps)
{
  float c = gps.course.isValid() ? gps.course.deg() : 0;

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  tft.drawString("COURSE", 120, 40, 4);
  tft.drawString(String(c, 0) + "°", 120, 140, 6);
}

static void drawPosition(TFT_eSPI &tft, TinyGPSPlus &gps)
{
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.drawString("LAT:", 10, 40, 4);
  tft.drawString(String(gps.location.lat(), 6), 10, 80, 4);

  tft.drawString("LON:", 10, 140, 4);
  tft.drawString(String(gps.location.lng(), 6), 10, 180, 4);
}

static void drawStatus(TFT_eSPI &tft, TinyGPSPlus &gps)
{
  tft.setTextDatum(TL_DATUM);

  tft.drawString("SAT: " + String(gps.satellites.value()), 10, 40, 4);
  tft.drawString("HDOP: " + String(gps.hdop.hdop(), 1), 10, 90, 4);
  tft.drawString("ALT: " + String(gps.altitude.meters(), 0) + " m", 10, 140, 4);
}

void drawScreen(TFT_eSPI &tft, TinyGPSPlus &gps, int page)
{
  switch (page)
  {
    case 0: drawSpeed(tft, gps); break;
    case 1: drawCourse(tft, gps); break;
    case 2: drawPosition(tft, gps); break;
    case 3: drawStatus(tft, gps); break;
  }
}
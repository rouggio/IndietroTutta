#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include "wifi_manager.h"

#include "screen_one.h"

extern TFT_eSPI tft;

// ==== COLORS ====
#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define GREEN TFT_GREEN
#define CYAN TFT_CYAN
#define DARK_RED 0x8000
#define GRAY 0x7BEF

static void maybeClear(int newState)
{
  static int lastState = -1;
  if (newState != lastState)
  {
    lastState = newState;
    tft.fillScreen(TFT_BLACK);
  }
}

// ====== LAYOUT ======
void drawTopBar(TinyGPSPlus &gps)
{
  int sats = gps.satellites.isValid() ? gps.satellites.value() : 0;

  // WiFi icon (top-left)
  if (wifiConnected())
  {
    tft.fillRoundRect(2, 2, 30, 24, 4, TFT_DARKGREEN);
    tft.drawRoundRect(2, 2, 30, 24, 4, GREEN);
    tft.setTextColor(GREEN, TFT_DARKGREEN);
    //todo check if this is the right place to draw the WiFi icon
    // tft.drawString("WiFi", 13, 18, 2);
  }
  else
  {
    tft.fillRoundRect(2, 2, 30, 24, 4, DARK_RED);
    tft.drawRoundRect(2, 2, 30, 24, 4, TFT_RED);
    tft.setTextColor(TFT_RED, DARK_RED);
    // todo place an icon for WiFi disconnected
    tft.drawString("-", 13, 18, 2);
  }

  // GPS satellites (top-right)
  tft.setTextColor(sats > 0 ? sats > 5 ? GREEN : TFT_YELLOW : TFT_RED, BG);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(String(sats), tft.width() - 10, 5, 4);

  // small sat icon (simple dot cluster)
  // todo add an icon for GPS satellites
  tft.fillCircle(tft.width() - 45, 18, 3, CYAN);
  tft.fillCircle(tft.width() - 38, 12, 2, CYAN);
  tft.fillCircle(tft.width() - 38, 22, 2, CYAN);

  tft.drawFastHLine(0, 30, tft.width(), GRAY);
}

void drawSpeed(float speedKn)
{

  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(WHITE, BG);
  tft.drawString("SPEED (kn)", tft.width() / 2, 50, 2);

  tft.setTextColor(TFT_YELLOW, BG);
  String spd = " " + String(speedKn, 1) + " ";
  tft.drawString(spd, tft.width() / 2, 100, 8);
}

void drawCourse(TinyGPSPlus &gps)
{
  String bearing = String(gps.course.deg());
  String dirName = gps.course.isValid() ? TinyGPSPlus::cardinal(gps.course.deg()) : "-";
  String courseString = bearing + " (" + dirName + ")";

  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(MC_DATUM);

  tft.drawString("COURSE", 50, 160, 2);
  tft.drawString(courseString, 150, 160, 4);
}

void drawBottomBar(String timeStr, String dateStr)
{
  tft.drawFastHLine(20, 200, tft.width() - 40, GRAY);

  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(BC_DATUM);

  String bottom = timeStr + "   |   " + dateStr;
  tft.drawString(bottom, tft.width() / 2, 240, 4);
}

void drawScreenOne(TinyGPSPlus &gps)
{
  drawTopBar(gps);
  drawSpeed(gps.speed.isValid() ? gps.speed.knots() : 0);
  drawCourse(gps);
  drawBottomBar(gps.time.isValid() ? String(gps.time.hour()) + ":" + String(gps.time.minute()) + ":" + String(gps.time.second()) : "--:--:--",
                gps.date.isValid() ? String(gps.date.day()) + "/" + String(gps.date.month()) + "/" + String(gps.date.year()) : "--/--/----");
}
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>

#include "config_store.h"
#include "screens.h"
#include "screen_config.h"

extern TFT_eSPI tft;

// ==== COLORS ====
#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define GRAY 0x7BEF

static bool configNeedsRedraw = true;
static String lastStatusLine = "";

void drawScreenConfig(bool requiresInit)
{
  if (requiresInit) {
    tft.fillScreen(BG);
    configNeedsRedraw = true;
  }

  // Reprint only when the connection status line changes
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
  String ssid = WiFi.SSID();
  String statusLine = (WiFi.status() == WL_CONNECTED) ? (ssid + " " + ip) : String("not connected");

  if (!configNeedsRedraw && statusLine == lastStatusLine) {
    return;
  }
  bool fullRedraw = configNeedsRedraw || statusLine != lastStatusLine;
  lastStatusLine = statusLine;

  if (fullRedraw) {
    tft.fillScreen(BG);
    configNeedsRedraw = false;

    tft.setTextColor(WHITE, BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("CONFIG", tft.width() / 2, 30, 4);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(GRAY, BG);
    tft.setTextSize(1);
    tft.drawString("1. Join WiFi network:", 20, 80, 2);

    tft.setTextColor(WHITE, BG);
    tft.drawString("IndietroTutta", 40, 102, 2);

    tft.setTextColor(GRAY, BG);
    tft.drawString("2. Open the portal:", 20, 128, 2);

    tft.setTextColor(WHITE, BG);
    tft.drawString("http://192.168.4.1", 40, 150, 2);

    tft.setTextColor(GRAY, BG);
    tft.drawString("Device name:", 20, 182, 2);
    tft.setTextColor(WHITE, BG);
    tft.drawString(config.username[0] ? config.username : "-", 140, 182, 2);
  }

  // Live connection status line
  tft.fillRect(0, 204, tft.width(), 16, BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(WiFi.status() == WL_CONNECTED ? TFT_GREEN : GRAY, BG);
  tft.drawString(statusLine, tft.width() / 2, 212, 2);

  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(BL_DATUM);
  tft.drawString("l Back", 8, 235, 2);
}

void screenConfigButton(Button button, ButtonEvent event)
{
  // Left short: back to MAIN
  if (button == Button::Left && event == ButtonEvent::ShortPress) {
    setCurrentPage(PageMain);
    return;
  }
}

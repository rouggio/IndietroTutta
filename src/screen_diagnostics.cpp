#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <WiFi.h>

#include "config.h"
#include "config_store.h"
#include "screens.h"
#include "screen_diagnostics.h"

extern TFT_eSPI tft;

// ==== COLORS ====
#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define GRAY 0x7BEF

static const int COL_L_X = 10;
static const int COL_R_X = 166;
static const int DIVIDER_X = 156;

// Fixed-width lines so shrinking values never leave stale pixels
static String padRight(String s, int len)
{
  while ((int)s.length() < len) s += " ";
  return s;
}

void drawScreenDiagnosticsMain(TinyGPSPlus &gps)
{
  // Page title, consistent with the other screens
  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("DIAGNOSTICS", tft.width() / 2, 20, 4);

  tft.drawFastHLine(20, 44, tft.width() - 40, GRAY);
  tft.drawFastVLine(DIVIDER_X, 52, 152, GRAY);

  char buf[24];

  // ---- Left column: GPS ----
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(GRAY, BG);
  tft.drawString("GPS", COL_L_X, 56, 2);

  tft.setTextColor(WHITE, BG);

  snprintf(buf, sizeof(buf), "Chars %lu", (unsigned long)gps.charsProcessed());
  tft.drawString(padRight(buf, 16), COL_L_X, 80, 2);

  if (gps.hdop.isValid())
    snprintf(buf, sizeof(buf), "HDOP %.1f", gps.hdop.hdop());
  else
    snprintf(buf, sizeof(buf), "HDOP --");
  tft.drawString(padRight(buf, 16), COL_L_X, 104, 2);

  if (gps.location.isValid())
    snprintf(buf, sizeof(buf), "Lat %.5f", gps.location.lat());
  else
    snprintf(buf, sizeof(buf), "Lat --");
  tft.drawString(padRight(buf, 16), COL_L_X, 128, 2);

  if (gps.location.isValid())
    snprintf(buf, sizeof(buf), "Lon %.5f", gps.location.lng());
  else
    snprintf(buf, sizeof(buf), "Lon --");
  tft.drawString(padRight(buf, 16), COL_L_X, 152, 2);

  if (gps.altitude.isValid())
    snprintf(buf, sizeof(buf), "Alt %.1fm", gps.altitude.meters());
  else
    snprintf(buf, sizeof(buf), "Alt --");
  tft.drawString(padRight(buf, 16), COL_L_X, 176, 2);

  if (gps.location.isValid())
    snprintf(buf, sizeof(buf), "Age %lums", (unsigned long)gps.location.age());
  else
    snprintf(buf, sizeof(buf), "Age --");
  tft.drawString(padRight(buf, 16), COL_L_X, 200, 2);

  // ---- Right column: SYSTEM ----
  tft.setTextColor(GRAY, BG);
  tft.drawString("SYSTEM", COL_R_X, 56, 2);

  tft.setTextColor(WHITE, BG);
  tft.drawString(padRight("VER " BUILD_VERSION, 14), COL_R_X, 80, 2);

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  tft.drawString(padRight("MAC " + mac, 14), COL_R_X, 104, 2);

  tft.drawString(padRight("IP " + WiFi.localIP().toString(), 14), COL_R_X, 128, 2);

  String ssid = WiFi.SSID();
  if (ssid.length() > 10) ssid = ssid.substring(0, 10);
  tft.drawString(padRight("SSID " + ssid, 14), COL_R_X, 152, 2);

  String user = config.username;
  if (user.length() > 9) user = user.substring(0, 9);
  tft.drawString(padRight("User " + user, 14), COL_R_X, 176, 2);

  // Hint bar: L/LL left, R/RR right (subscreen: L returns to speed)
  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(BL_DATUM);
  tft.drawString("L Back", 8, 235, 2);
}

void drawScreenDiagnostics(TinyGPSPlus &gps)
{
  drawScreenDiagnosticsMain(gps);
}

void screenDiagnosticsButton(
    Button button,
    ButtonEvent event
) {
    // Subscreen of speed: L returns to speed screen
    if (button == Button::Left && event == ButtonEvent::ShortPress) {
        setCurrentPage(PageMain);
        return;
    }
}

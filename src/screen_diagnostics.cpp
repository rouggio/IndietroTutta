#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <WiFi.h>

#include "config.h"
#include "config_store.h"
#include "screens.h"
#include "screen_diagnostics.h"

#include "canvas.h"

// ==== COLORS ====
#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define GRAY 0x7BEF

// Two equal columns on the 480px panel: 10px margin, 225px each, divider dead
// centre at 240.
static const int COL_L_X = 10;
static const int COL_R_X = 250;
static const int DIVIDER_X = 240;

static const int HDR_Y = 50;    // column titles ("GPS" / "SYSTEM"), font 2
static const int ROW_Y0 = 74;   // first value row, font 4 (26px tall)
static const int ROW_DY = 34;   // 6 rows: 74..270, clear of the panel edge

// Fixed-width lines so shrinking values never leave stale pixels
static String padRight(String s, int len)
{
  while ((int)s.length() < len) s += " ";
  return s;
}

void drawScreenDiagnosticsMain(TinyGPSPlus &gps)
{
  tft.drawFastHLine(0, 44, tft.width(), GRAY);
  tft.drawFastVLine(DIVIDER_X, HDR_Y - 2, 320 - (HDR_Y - 2), GRAY);

  char buf[28];

  // ---- Left column: GPS ----
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(GRAY, BG);
  tft.drawString("GPS", COL_L_X, HDR_Y, 2);

  tft.setTextColor(WHITE, BG);

  snprintf(buf, sizeof(buf), "Chars %lu", (unsigned long)gps.charsProcessed());
  tft.drawString(padRight(buf, 15), COL_L_X, ROW_Y0, 4);

  if (gps.hdop.isValid())
    snprintf(buf, sizeof(buf), "HDOP %.1f", gps.hdop.hdop());
  else
    snprintf(buf, sizeof(buf), "HDOP --");
  tft.drawString(padRight(buf, 15), COL_L_X, ROW_Y0 + ROW_DY, 4);

  if (gps.location.isValid())
    snprintf(buf, sizeof(buf), "Lat %.5f", gps.location.lat());
  else
    snprintf(buf, sizeof(buf), "Lat --");
  tft.drawString(padRight(buf, 15), COL_L_X, ROW_Y0 + 2 * ROW_DY, 4);

  if (gps.location.isValid())
    snprintf(buf, sizeof(buf), "Lon %.5f", gps.location.lng());
  else
    snprintf(buf, sizeof(buf), "Lon --");
  tft.drawString(padRight(buf, 15), COL_L_X, ROW_Y0 + 3 * ROW_DY, 4);

  if (gps.altitude.isValid())
    snprintf(buf, sizeof(buf), "Alt %.1fm", gps.altitude.meters());
  else
    snprintf(buf, sizeof(buf), "Alt --");
  tft.drawString(padRight(buf, 15), COL_L_X, ROW_Y0 + 4 * ROW_DY, 4);

  if (gps.location.isValid())
    snprintf(buf, sizeof(buf), "Age %lums", (unsigned long)gps.location.age());
  else
    snprintf(buf, sizeof(buf), "Age --");
  tft.drawString(padRight(buf, 15), COL_L_X, ROW_Y0 + 5 * ROW_DY, 4);

  // ---- Right column: SYSTEM ----
  tft.setTextColor(GRAY, BG);
  tft.drawString("SYSTEM", COL_R_X, HDR_Y, 2);

  tft.setTextColor(WHITE, BG);
  tft.drawString(padRight("VER: " BUILD_VERSION, 15), COL_R_X, ROW_Y0, 4);

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  // 12 hex chars + "MAC: " does not fit the 225px column at font 4, so this
  // line alone drops to font 2 (measured, not guessed).
  String macLine = "MAC: " + mac;
  const uint8_t macFont =
      tft.textWidth(macLine, 4) <= (480 - COL_R_X - 6) ? 4 : 2;
  tft.drawString(padRight(macLine, 15), COL_R_X, ROW_Y0 + ROW_DY, macFont);

  tft.drawString(padRight("IP: " + WiFi.localIP().toString(), 15), COL_R_X,
                 ROW_Y0 + 2 * ROW_DY, 4);

  String ssid = WiFi.SSID();
  if (ssid.length() > 10) ssid = ssid.substring(0, 10);
  tft.drawString(padRight("SSID: " + ssid, 15), COL_R_X, ROW_Y0 + 3 * ROW_DY, 4);

  String user = config.username;
  if (user.length() > 9) user = user.substring(0, 9);
  tft.drawString(padRight("User: " + user, 15), COL_R_X, ROW_Y0 + 4 * ROW_DY, 4);
  // No hint bar: the buttons are on the right edge of the enclosure.
}

void drawScreenDiagnostics(TinyGPSPlus &gps, bool requiresInit)
{
  // The title bar is chrome: drawing it on every 200ms pass makes the fill +
  // text re-render and the navy band visibly flickers (the race menu draws its
  // title only on a full pass, which is why it does not). Repaint it only when
  // the page is (re)entered.
  static bool chromeDrawn = false;
  if (requiresInit) chromeDrawn = false;

  if (!chromeDrawn) {
    tft.fillRect(0, 0, tft.width(), 44, brandNavy());
    tft.setTextColor(WHITE, brandNavy());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("DIAGNOSTICS", tft.width() / 2, 22, 4);
    tft.drawFastHLine(0, 44, tft.width(), GRAY);
    chromeDrawn = true;
  }
  drawScreenDiagnosticsMain(gps);
}

void screenDiagnosticsButton(
    Button button,
    ButtonEvent event
) {
    // Subscreen of speed: 1S returns to the speed screen
    if (button == Button::Left && event == ButtonEvent::ShortPress) {
        setCurrentPage(PageMain);
        return;
    }
}

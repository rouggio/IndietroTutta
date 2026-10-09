#include <Arduino.h>
#include <TFT_eSPI.h>

#include "splash_screen.h"
#include "splash_logo.h"

#include "canvas.h"

// Frontend navy (#0f172a) so the logo square blends in seamlessly
static uint16_t splashBg()
{
  return tft.color565(15, 23, 42);
}

void drawSplashScreen() {
  tft.fillScreen(splashBg());

  // "Indietro <logo> Tutta!" — measured at runtime, centered as one row
  const int gap = 10;
  int w1 = tft.textWidth("Indietro", 4);
  int w2 = tft.textWidth("Tutta!", 4);
  int totalW = w1 + gap + SPLASH_LOGO_W + gap + w2;
  int x = (tft.width() - totalW) / 2;
  int logoY = (tft.height() - SPLASH_LOGO_H) / 2;
  int textY = logoY + (SPLASH_LOGO_H - 26) / 2;

  tft.setTextColor(TFT_WHITE, splashBg());
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Indietro", x, textY, 4);
  x += w1 + gap;
  canvasPushImage(x, logoY, SPLASH_LOGO_W, SPLASH_LOGO_H, (uint16_t*)splashLogo);
  x += SPLASH_LOGO_W + gap;
  tft.drawString("Tutta!", x, textY, 4);
}
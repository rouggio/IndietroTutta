#include <Arduino.h>
#include <TFT_eSPI.h>

#include "splash_screen.h"

extern TFT_eSPI tft;

void drawSplashScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Indietro Tutta!", 160, 120, 4);
  delay(2000);
  tft.fillScreen(TFT_BLACK);
}
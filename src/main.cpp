#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>

#include "gps.h"
#include "screens.h"

TFT_eSPI tft = TFT_eSPI();
TinyGPSPlus gps;

unsigned long lastPageChange = 0;
unsigned long lastUpdate = 0;

int page = 0;
const int NUM_PAGES = 4;

void setup() {
  Serial.begin(115200);

  GPS_begin();

  tft.init();
  tft.invertDisplay(false);
  tft.setRotation(1);

  drawSplash(tft);
  delay(1500);
}

void loop() {
  GPS_update(gps);

  // cambio pagina ogni 3 secondi
  if (millis() - lastPageChange > 3000) {
    lastPageChange = millis();
    page = (page + 1) % NUM_PAGES;
    tft.fillScreen(TFT_BLACK);
  }

  // refresh display
  if (millis() - lastUpdate > 200) {
    lastUpdate = millis();
    drawScreen(tft, gps, page);
  }
}
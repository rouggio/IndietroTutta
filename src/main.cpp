#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <SPI.h>

#include "gps.h"
#include "screens.h"

TFT_eSPI tft = TFT_eSPI();
TinyGPSPlus gps;

const int SCREEN_BUTTON_PIN = 22;
const unsigned long BUTTON_DEBOUNCE_MS = 250;

unsigned long lastButtonChange = 0;
unsigned long lastUpdate = 0;

int page = 0;
const int NUM_PAGES = 2;
bool lastButtonState = HIGH;

void setup(void) {
  Serial.begin(115200);
  delay(1000);

  GPS_begin();

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(false);
  pinMode(SCREEN_BUTTON_PIN, INPUT_PULLUP);

  drawSplash(tft);
}

void loop() {
  GPS_update(gps);

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
    drawScreen(tft, gps, page);
  }

}
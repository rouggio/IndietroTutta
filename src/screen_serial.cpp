#include "screen_serial.h"
#include "serial_buffer.h"
#include "config.h"
#include <TFT_eSPI.h>

static TFT_eSPI tft = TFT_eSPI();
static bool initialized = false;

void drawScreenSerial(TinyGPSPlus &gps, bool requiresInit) {
  if (requiresInit || !initialized) {
    tft.init();
    tft.setRotation(1); // landscape
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    initialized = true;
  }

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);

  int w = tft.width();
  int h = tft.height();

  // approximate line height for textSize 1
  const int lineH = 8;
  const int maxLines = h / lineH; // fits on screen

  int total = serialLinesCount();
  int start = total - maxLines;
  if (start < 0) start = 0;

  // draw newest at bottom-like terminal
  int y = 0;
  tft.setTextDatum(TL_DATUM);
  for (int i = 0; i < maxLines; ++i) {
    int idx = start + i;
    String s = (idx < total) ? serialLine(idx) : String("");
    tft.drawString(s, 0, y);
    y += lineH;
  }

  // small footer
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Serial buffer", 2, h - lineH);
}

void screenSerialButton(Button button, ButtonEvent event) {
  // simple controls: long press Left clears buffer
  if (event == ButtonEvent::LongPress) {
    if (button == Button::Left) {
      clearSerialBuffer();
    }
  }
}

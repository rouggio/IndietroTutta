#include <Arduino.h>
#include <TFT_eSPI.h>

#include "screens.h"
#include "screen_timers.h"

extern TFT_eSPI tft;

// ==== COLORS ====
#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define GRAY 0x7BEF

// --------------------------------------------------
// Chronograph state
// --------------------------------------------------

enum class ChronoState {
  Idle,
  Running,
  Stopped
};

static ChronoState chronoState = ChronoState::Idle;
static unsigned long chronoSegmentStart = 0; // millis() when current run began
static unsigned long chronoElapsedMs = 0;    // accumulated time of previous runs

struct Lap {
  unsigned long lapMs;   // time of the single lap
  unsigned long totalMs; // total elapsed at the lap point
};

static constexpr int MAX_LAPS = 10;
static Lap laps[MAX_LAPS];
static int lapCount = 0;

static unsigned long chronoNow()
{
  unsigned long now = chronoElapsedMs;

  if (chronoState == ChronoState::Running) {
    now += millis() - chronoSegmentStart;
  }

  return now;
}

// MM:SS when under one hour, H:MM:SS otherwise
static String formatBig(unsigned long ms)
{
  unsigned long totalSeconds = ms / 1000;
  unsigned long hours = totalSeconds / 3600;
  unsigned long minutes = (totalSeconds % 3600) / 60;
  unsigned long seconds = totalSeconds % 60;

  String mm = minutes < 10 ? "0" + String(minutes) : String(minutes);
  String ss = seconds < 10 ? "0" + String(seconds) : String(seconds);

  if (hours > 0) {
    return String(hours) + ":" + mm + ":" + ss;
  }

  return mm + ":" + ss;
}

// M:SS.t (tenths)
static String formatLap(unsigned long ms)
{
  unsigned long totalSeconds = ms / 1000;
  unsigned long minutes = totalSeconds / 60;
  unsigned long seconds = totalSeconds % 60;
  unsigned long tenths = (ms % 1000) / 100;

  return String(minutes) + ":" +
         (seconds < 10 ? "0" : "") + String(seconds) + "." + String(tenths);
}

void drawScreenTimers(bool requiresInit)
{
  if (requiresInit) {
    tft.fillScreen(BG);
  }

  const unsigned long now = chronoNow();

  // Status tag
  tft.setTextDatum(MC_DATUM);
  if (chronoState == ChronoState::Running) {
    tft.setTextColor(TFT_GREEN, BG);
    tft.drawString("RUNNING", tft.width() / 2, 40, 4);
  } else if (chronoState == ChronoState::Stopped) {
    tft.setTextColor(TFT_ORANGE, BG);
    tft.drawString("STOPPED", tft.width() / 2, 40, 4);
  } else {
    tft.setTextColor(GRAY, BG);
    tft.drawString("READY", tft.width() / 2, 40, 4);
  }

  // Big time, centiseconds underneath
  tft.setTextColor(WHITE, BG);
  tft.drawString(formatBig(now), tft.width() / 2, 105, 7);

  char cs[3];
  snprintf(cs, sizeof(cs), "%02lu", (now % 1000) / 10);
  tft.setTextColor(GRAY, BG);
  tft.drawString("." + String(cs), tft.width() / 2, 155, 4);

  // Laps list: up to the last four, oldest first
  tft.drawFastHLine(20, 190, tft.width() - 40, GRAY);

  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(TL_DATUM);

  const int shown = 4;
  int first = lapCount - shown;
  if (first < 0) first = 0;

  for (int i = first; i < lapCount; i++) {
    int y = 198 + (i - first) * 12;
    String row = "L" + String(i + 1) + "  " +
                 formatLap(laps[i].lapMs) + "  /  " +
                 formatLap(laps[i].totalMs);
    tft.drawString(row, 20, y, 1);
  }

  // Bottom labels
  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(BC_DATUM);
  tft.drawString("L: Page", 8, 235, 2);
  tft.setTextDatum(BR_DATUM);
  tft.setTextSize(1);
  tft.drawString("RS Run/Stop  RLB Lap  LLC Clear", tft.width() - 8, 238, 1);
}

void screenTimersButton(Button button, ButtonEvent event)
{
  // Left short: advance to the next page
  if (button == Button::Left && event == ButtonEvent::ShortPress) {
    nextScreen();
    return;
  }

  // Right short: start / stop
  if (button == Button::Right && event == ButtonEvent::ShortPress) {
    if (chronoState == ChronoState::Running) {
      chronoElapsedMs += millis() - chronoSegmentStart;
      chronoState = ChronoState::Stopped;
    } else {
      chronoSegmentStart = millis();
      chronoState = ChronoState::Running;
    }
    return;
  }

  // Left long: clear everything
  if (button == Button::Left && event == ButtonEvent::LongPress) {
    chronoState = ChronoState::Idle;
    chronoElapsedMs = 0;
    chronoSegmentStart = 0;
    lapCount = 0;
    tft.fillScreen(BG);
    return;
  }

  // Right long: record a lap while running
  if (button == Button::Right && event == ButtonEvent::LongPress) {
    if (chronoState != ChronoState::Running) {
      return;
    }

    const unsigned long total = chronoNow();
    const unsigned long previousTotal = lapCount > 0 ? laps[lapCount - 1].totalMs : 0;

    if (lapCount == MAX_LAPS) {
      for (int i = 1; i < MAX_LAPS; i++) {
        laps[i - 1] = laps[i];
      }
      lapCount--;
    }

    laps[lapCount++] = { total - previousTotal, total };
    return;
  }
}

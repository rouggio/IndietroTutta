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

// Ghost-prevention: these lines are repainted every frame, but when the
// new content is narrower than what was drawn before, the leftover
// pixels are consciously wiped first.
static bool statusDrawn = false;
static ChronoState drawnState = ChronoState::Idle;
static String lastBigTime = "";

// Layout: time lives on the left half, laps in a column on the
// right side so neither overflows into the bottom hint bar.
static constexpr int TIME_CENTER_X = 78;
static constexpr int LAPS_X = 176;
static constexpr int LAPS_DIVIDER_X = 166;
static constexpr int LAPS_MAX_SHOWN = 7;

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

  // ---- Left half: status + time ----

  if (!statusDrawn || drawnState != chronoState) {
    tft.fillRect(TIME_CENTER_X - 55, 24, 110, 22, BG);
    drawnState = chronoState;
    statusDrawn = true;
  }

  tft.setTextDatum(MC_DATUM);
  if (chronoState == ChronoState::Running) {
    tft.setTextColor(TFT_GREEN, BG);
    tft.drawString("RUNNING", TIME_CENTER_X, 35, 2);
  } else if (chronoState == ChronoState::Stopped) {
    tft.setTextColor(TFT_ORANGE, BG);
    tft.drawString("STOPPED", TIME_CENTER_X, 35, 2);
  } else {
    tft.setTextColor(GRAY, BG);
    tft.drawString("READY", TIME_CENTER_X, 35, 2);
  }

  String bigTime = formatBig(now);

  if (bigTime.length() < lastBigTime.length()) {
    tft.fillRect(TIME_CENTER_X - 85, 62, 170, 58, BG);
  }
  lastBigTime = bigTime;

  tft.setTextColor(WHITE, BG);
  tft.drawString(bigTime, TIME_CENTER_X, 95, 6);

  char cs[3];
  snprintf(cs, sizeof(cs), "%02lu", (now % 1000) / 10);
  tft.setTextColor(GRAY, BG);
  tft.drawString("." + String(cs), TIME_CENTER_X, 140, 4);

  // ---- Right column: laps (newest last) ----
  tft.drawFastVLine(LAPS_DIVIDER_X, 28, 160, GRAY);

  const int shown = (lapCount < LAPS_MAX_SHOWN) ? lapCount : LAPS_MAX_SHOWN;
  int first = lapCount - shown;

  for (int i = first; i < lapCount; i++) {
    int y = 32 + (i - first) * 22;

    tft.setTextColor(TFT_YELLOW, BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("L" + String(i + 1), LAPS_X, y, 2);

    tft.setTextColor(WHITE, BG);
    tft.drawString(formatLap(laps[i].lapMs), LAPS_X, y + 14, 1);

    tft.setTextColor(GRAY, BG);
    tft.drawString("/ " + formatLap(laps[i].totalMs), LAPS_X + 52, y + 14, 1);
  }

  // ---- Hint bar ----
  tft.setTextColor(WHITE, BG);
  tft.setTextDatum(BL_DATUM);
  tft.drawString("L - Next", 8, 235, 2);
  tft.setTextDatum(BR_DATUM);
  tft.drawString("R Run/Stop  RL Lap/Rst", tft.width() - 8, 235, 2);
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

  // Right long: record a lap while running, reset everything otherwise
  if (button == Button::Right && event == ButtonEvent::LongPress) {
    if (chronoState != ChronoState::Running) {
      chronoState = ChronoState::Idle;
      chronoElapsedMs = 0;
      chronoSegmentStart = 0;
      lapCount = 0;
      tft.fillScreen(BG);
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

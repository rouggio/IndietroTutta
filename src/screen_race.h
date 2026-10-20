#pragma once

#include <TinyGPSPlus.h>
#include "buttons.h"

// Shared race screen for practice + race (same engine, only the tag and
// the startTime source differ). Step 3: wireframe + boat + wind + GPS
// countdown + next-mark data. Pass/fail arrives in Step 5.
void drawScreenRace(TinyGPSPlus &gps, bool requiresInit);
void screenRaceButton(Button button, ButtonEvent event);
// Todo 38: the finished-race hold (banner + frozen time) lives until the next
// run/session or until the page is left; screens.cpp calls this on the way out.
void raceFinishHoldClear();

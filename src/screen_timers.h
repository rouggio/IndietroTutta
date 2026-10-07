#pragma once

#include <Arduino.h>
#include "buttons.h"

void drawScreenTimers(bool requiresInit);
void screenTimersButton(Button button, ButtonEvent event);

// True while the chronograph is running (used by the main screen indicator)
bool chronoIsRunning();
// Current chrono text MM:SS.t (used by the main screen while running)
String chronoDisplayText();

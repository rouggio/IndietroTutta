#pragma once

#include <TinyGPSPlus.h>
#include "buttons.h"

void drawScreenSpeed(TinyGPSPlus &gps, bool requiresInit);

void screenSpeedButton(Button button, ButtonEvent event);

// Ring view state, for /status: the mode `2S` selected ("north"/"head"/"wind"),
// the view actually drawn (differs from the mode while that view's input is
// missing) and the availability the button uses to skip an undrawable view.
const char *ringViewModeName();
const char *ringViewDrawnName();
bool ringViewCanHeading();
bool ringViewCanWind();
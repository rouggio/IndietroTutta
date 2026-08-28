#pragma once

#include <TinyGPSPlus.h>
#include "buttons.h"

void drawScreenSpeed(TinyGPSPlus &gps, bool requiresInit);

void screenSpeedButton(Button button, ButtonEvent event);
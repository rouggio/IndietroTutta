#pragma once

#include <TinyGPSPlus.h>
#include "buttons.h"

void drawScreenWaypoints(TinyGPSPlus& gps, bool requiresInit);
void screenWaypointsButton(Button button, ButtonEvent event);

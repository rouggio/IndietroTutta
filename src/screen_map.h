#pragma once

#include <TinyGPSPlus.h>
#include "buttons.h"

void drawScreenMap(TinyGPSPlus &gps, bool requiresInit);

void screenMapButton(Button button, ButtonEvent event);

#pragma once

#include <TinyGPSPlus.h>
#include "buttons.h"

void drawScreenOne(TinyGPSPlus &gps, bool requiresInit);

void screenOneButton(Button button, ButtonEvent event);
#pragma once

#include <TinyGPSPlus.h>
#include "buttons.h"

void drawScreenSerial(TinyGPSPlus &gps, bool requiresInit);
void screenSerialButton(Button button, ButtonEvent event);

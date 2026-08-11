#pragma once
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include "buttons.h"

void drawSplash();
void screenInit();
void screenLoop(TinyGPSPlus &gps);

void drawScreen(
    TinyGPSPlus &gps,
    bool requiresInit,
    int page
);

void screenButtonEvent(
    Button button,
    ButtonEvent event
);
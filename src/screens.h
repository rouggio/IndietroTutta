#pragma once
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include "buttons.h"

enum class UIState {
    Screens,
    Menu
};

void drawSplash();
void screenInit();
void screenLoop(TinyGPSPlus &gps);
void nextScreen();

void drawScreen(
    TinyGPSPlus &gps,
    bool requiresInit,
    int page
);

void screenButtonEvent(
    Button button,
    ButtonEvent event
);
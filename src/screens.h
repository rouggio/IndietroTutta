#pragma once
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include "buttons.h"

enum class UIState {
    Screens,
    Menu
};

// The top-level pages cycle with a left short click.
enum ScreenPage {
    PageMain = 0,
    PageDiagnostics = 1,
    PageWaypoints = 2,
    PageTimers = 3
};

static constexpr int PAGE_CYCLE = 4; // number of pages in the L-short cycle

// Non-blocking splash: drawn once at boot, kept on screen while setup
// runs underneath; endSplash() releases it as soon as the loop is ready
// (subject to a short minimum display time).
void beginSplash();
void endSplash();
bool splashActive();

void screenInit();
void screenLoop(TinyGPSPlus &gps);
void nextScreen();

void drawScreen(
    TinyGPSPlus &gps,
    bool requiresInit,
    ScreenPage page
);

void screenButtonEvent(
    Button button,
    ButtonEvent event
);

// Programmatically set the current page. Clears the display and forces
// a full redraw on the next loop pass.
void setCurrentPage(ScreenPage p);
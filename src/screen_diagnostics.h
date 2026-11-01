#pragma once

#include <TinyGPSPlus.h>
#include "buttons.h"

void drawScreenDiagnostics(TinyGPSPlus &gps, bool requiresInit);

void screenDiagnosticsButton(Button button, ButtonEvent event);
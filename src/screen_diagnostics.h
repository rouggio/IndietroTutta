#pragma once

#include <TinyGPSPlus.h>
#include "buttons.h"

void drawScreenDiagnostics(TinyGPSPlus &gps);

void screenDiagnosticsButton(Button button, ButtonEvent event);
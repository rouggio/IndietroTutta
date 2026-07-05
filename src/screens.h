#pragma once
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>

void drawSplash();
void screenInit();
void screenLoop(TinyGPSPlus &gps);
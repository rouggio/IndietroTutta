#pragma once
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>

void drawSplash(TFT_eSPI &tft);
void drawScreen(TFT_eSPI &tft, TinyGPSPlus &gps, int page);
#pragma once

#include <TinyGPSPlus.h>

void httpServerInit(TinyGPSPlus &gps);
void httpServerLoop();
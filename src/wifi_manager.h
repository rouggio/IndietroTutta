#pragma once

#include <TinyGPSPlus.h>

void wifiInit(TinyGPSPlus &gps);
void wifiLoop();
bool wifiConnected();
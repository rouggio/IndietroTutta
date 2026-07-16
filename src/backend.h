#pragma once

#include <TinyGPSPlus.h>

bool backendOnline();

void backendInit();

void backendLoop(TinyGPSPlus &gps);
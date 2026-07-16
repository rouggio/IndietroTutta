#pragma once

#include <Arduino.h>

constexpr int MAX_NMEA_LINES = 30;

extern String nmeaLines[MAX_NMEA_LINES];

int getNextNMEALine();
void processNMEAChar(char c);
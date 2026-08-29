#pragma once
#include <Arduino.h>

// Called from backend health task when /health returns race/course.
// serverTimeIso is the server's ISO time (for fallback sync when no GPS fix).
void raceUpdateFromHealth(const String& id, const String& name, const String& status, const String& startTimeIso, const String& courseName, const String& serverTimeIso);
void raceClear();

bool raceHasActive();
String raceGetName();
String raceGetStatus();
unsigned long raceGetStartTimeMs(); // epoch ms, 0 if none
String raceGetCourseName();

// Time sync: GPS time if valid, else serverTime + millis()
unsigned long getSyncedTimeMs(class TinyGPSPlus &gps);
String formatCountdown(unsigned long remainingMs);

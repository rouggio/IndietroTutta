#pragma once
#include <Arduino.h>

struct RaceMark {
    double lat;
    double lon;
    int radius;
    String side; // P/S/G
};

void raceUpdateFromHealth(const String& id, const String& name, const String& status, const String& startTimeIso, const String& courseName, const String& courseMarksJson, const String& serverTimeIso);
void raceClear();

bool raceHasActive();
String raceGetName();
String raceGetStatus();
unsigned long raceGetStartTimeMs();
String raceGetCourseName();
int raceGetMarkCount();
int raceGetCurrentMarkIndex();
bool raceGetNextMark(RaceMark &out);
void raceAdvanceMark();
bool raceCheckPass(class TinyGPSPlus &gps); // call each loop, advances if within radius

// Time sync: GPS time if valid, else serverTime + millis()
unsigned long getSyncedTimeMs(class TinyGPSPlus &gps);
String formatCountdown(unsigned long remainingMs);

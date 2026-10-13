#include <TinyGPSPlus.h>

#include "wifi_manager.h"
#include "gps.h"
#include "http_server.h"
#include "backend.h"
#include "ota.h"
#include <WiFi.h>
#include "buttons.h"
#include "screens.h"
#include "serial_buffer.h"
#include "race_session.h"
#include "race_run.h"
#include "race_courses.h"

TinyGPSPlus gps;


void setup(void) {
  // Buzzer self-test (passive buzzer + on D19, - on GND): two short beeps.
  pinMode(19, OUTPUT);
  tone(19, 2500, 120);
  delay(200);
  tone(19, 2500, 120);
  screenInit();
  beginSplash();
  Serial.begin(115200);
  delay(1000); // Wait for Serial to initialize
  loadConfig(config);
  raceSessionLoad(); // reboot-safe course (updated by health polls)
  raceRunInit(); // live-session run state (laps/times)
  practicePrefsLoad(); // remembered practice course/gun/distance (NVS)
  
  bufferedSerialPrintln("Indietro Tutta");
  gpsInit();
  wifiInit(gps);
  buttonsInit();
  buttonsSetCallback(screenButtonEvent);
  backendInit(&gps);
  otaInit();
  endSplash(); // setup done: release the splash on the next loop pass
  bufferedSerialPrintln("Indietro Tutta - Setup Complete");
}

void loop() {
  serialBufferLoop();
  buttonsUpdate();
  gpsLoop(gps);
  // Keep the wall clock calibrated on every pass, whatever screen is up:
  // raceGpsEpoch() was previously only reached from race-screen paths, so
  // the clock (and log timestamps) never calibrated on the main screen.
  raceGpsEpoch(gps);
  screenLoop(gps);
  wifiLoop();
  backendLoop(gps);
  otaLoop();
}
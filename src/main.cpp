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
#include "gps_mock.h"
#include "sound.h"

TinyGPSPlus gps;


void setup(void) {
  screenInit();
  beginSplash();
  Serial.begin(115200);
  delay(1000); // Wait for Serial to initialize
  loadConfig(config);
  // Boot chime on the passive buzzer (+ on D19, - on GND). MUST come after
  // loadConfig(): that is where bootSoundOn is read from NVS, and playing
  // earlier would always use the compile-time default instead of the
  // stored preference. Queued, so it costs no boot time — it overlaps the
  // splash.
  soundStartup();
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
  // Release one due mock fix here, not on the backend task: this is the
  // fast loop. The backend task blocks ~2s per POST /gps, which held the
  // mock feed to ~0.3Hz and cost the track ~7 fixes every 20s. Emitting
  // from RAM is microseconds, so it does not disturb the UI cadence.
  // gpsMockTick() no-ops when mock is off (the check is inside).
  gpsMockTick(gps);
  screenLoop(gps);
  // Todo 30: a session newly assigned to this boat opens the race screen for
  // the sailor, so the fleet sees a race without touching the instrument.
  // Only ever FROM MAIN: DIAGNOSTICS and CONFIG are deliberate trips, and the
  // request is one-shot, so a sailor who leaves the race screen afterwards is
  // not dragged back.
  if (backendTakeRacePageRequest() && page == PageMain) {
    setCurrentPage(PageRace);
  }
  wifiLoop();
  backendLoop(gps);
  otaLoop();
}
#include <TinyGPSPlus.h>

#include "wifi_manager.h"
#include "gps_debug.h"
#include "gps.h"
#include "http_server.h"
#include "backend.h"
#include "ota.h"
#include <WiFi.h>
#include "buttons.h"
#include "screens.h"
#include "serial_buffer.h"

TinyGPSPlus gps;


void setup(void) {
  screenInit();
  beginSplash();
  Serial.begin(115200);
  delay(1000); // Wait for Serial to initialize
  loadConfig(config);
  
  bufferedSerialPrintln("Indietro Tutta");
  gpsInit();
  wifiInit(gps);
  buttonsInit();
  buttonsSetCallback(screenButtonEvent);
  backendInit();
  otaInit();
  endSplash(); // setup done: release the splash on the next loop pass
  bufferedSerialPrintln("Indietro Tutta - Setup Complete");
}

void loop() {
  serialBufferLoop();
  buttonsUpdate();
  gpsLoop(gps);
  screenLoop(gps);
  wifiLoop();
  backendLoop(gps);
  otaLoop();
}
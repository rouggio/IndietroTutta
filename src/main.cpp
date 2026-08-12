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

TinyGPSPlus gps;


void setup(void) {
  Serial.begin(115200);
  delay(1000); // Wait for Serial to initialize
  
  Serial.println("Indietro Tutta");
  gpsInit();
  wifiInit(gps);
  screenInit();
  drawSplash();
  buttonsInit();
  buttonsSetCallback(screenButtonEvent);
  backendInit();
  otaInit();
  Serial.println("Indietro Tutta - Setup Complete");
}

void loop() {
  buttonsUpdate();
  gpsLoop(gps);
  screenLoop(gps);
  wifiLoop();
  backendLoop(gps);
}
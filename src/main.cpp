#include <TinyGPSPlus.h>

#include "wifi_manager.h"
#include "gps_debug.h"
#include "gps.h"
#include "screens.h"
#include "http_server.h"
#include "backend.h"
#include <WiFi.h>

TinyGPSPlus gps;


void setup(void) {
  Serial.begin(115200);
  delay(1000); // Wait for Serial to initialize
  
  Serial.println("Indietro Tutta");
  gpsInit();
  screenInit();
  wifiInit(gps);
  drawSplash();
  backendInit();
  Serial.println("Indietro Tutta - Setup Complete");
}

void loop() {
  gpsLoop(gps);
  screenLoop(gps);
  wifiLoop();
  backendLoop(gps);
}
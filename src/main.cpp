#include <TinyGPSPlus.h>

#include "wifi_manager.h"
#include "gps.h"
#include "screens.h"
#include <WiFi.h>

TinyGPSPlus gps;


void setup(void) {
  Serial.begin(115200);
  delay(1000);

  gpsInit();
  screenInit();
  wifiInit();
  drawSplash();
}

void loop() {
  gpsLoop(gps);
  screenLoop(gps);
  wifiLoop();

  if (WiFi.isConnected()) {
    //todo implement restLoop() to send data to endpoint
    //restLoop();
  }
}
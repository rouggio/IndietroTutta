#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <TFT_eSPI.h>

#include "config.h"

#define CHECK_ON_BOOT true

extern TFT_eSPI tft;

String baseURL() {
  return OTA_BASE_URL;
}

String sanitizeVersion(const String& raw) {
  String clean = "";
  for (size_t i = 0; i < raw.length(); i++) {
    char c = raw.charAt(i);
    if ((c >= '0' && c <= '9') || c == '.') clean += c;
  }
  return clean;
}

String fetchServerVersion() {
  WiFiClientSecure client;
  HTTPClient http;

  client.setInsecure();

  String url = baseURL() + "/latest.txt";
  Serial.println("Checking version at: " + url);
  
  http.begin(client, url);
  int code = http.GET();
 
  if (code <= 0) {  // transient failure - retry once
    Serial.printf("latest.txt GET failed (%d), retrying...\n", code);
    http.end();
    delay(200);
    http.begin(client, url);
    code = http.GET();
  }
 
  String ver = "";
  if (code == HTTP_CODE_OK) {
    ver = http.getString();
    ver.trim();
    ver = sanitizeVersion(ver);  // strip BOM / whitespace / junk bytes
  } else {
    Serial.printf("latest.txt GET failed, code %d\n", code);
  }
  http.end();
  return ver;
}

bool isNewer(const String& server, const String& local) {
  int sMaj = 0, sMin = 0, sPatch = 0;
  int lMaj = 0, lMin = 0, lPatch = 0;

  sscanf(server.c_str(), "%d.%d.%d", &sMaj, &sMin, &sPatch);
  sscanf(local.c_str(),  "%d.%d.%d", &lMaj, &lMin, &lPatch);

  if (sMaj != lMaj) return sMaj > lMaj;
  if (sMin != lMin) return sMin > lMin;
  return sPatch > lPatch;
}

void drawOTAScreen(int progress) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Update progress: " + String(progress) + "%", 160, 120, 4);
}

void doUpdate() {
  String url = baseURL() + "/firmware.bin";
  Serial.println("Downloading: " + url);
 
  WiFiClientSecure client;
  client.setInsecure();
 
  httpUpdate.onProgress([](int cur, int total) {
    unsigned int pct = total ? (cur * 100) / total : 0;
    drawOTAScreen(pct);
    static int lastShownPct = -1;
    if (pct != (unsigned int)lastShownPct && pct % 10 == 0) {
      lastShownPct = pct;
    }
  });
 
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return ret = httpUpdate.update(client, url);
 
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("\nUpdate failed (%d): %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("\nNo updates");
      break;
    case HTTP_UPDATE_OK:
      // Board reboots automatically; this line rarely prints
      Serial.println("\nUpdate OK - rebooting");
      break;
  }
}

void checkForUpdate() {
  Serial.println("Checking for update...");
 
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
 
  String serverVer = fetchServerVersion();
  if (serverVer.length() == 0) {
    return;
  }
 
  Serial.println("Current version: " + String(BUILD_VERSION));
  Serial.println("Server version:  " + serverVer);
 
  if (isNewer(serverVer, BUILD_VERSION)) {
    Serial.println("Update available. Downloading update...");
    delay(800);
    doUpdate();
  } else {
    Serial.println("Up to date.");
  }
}

void otaInit() {
  if (CHECK_ON_BOOT) checkForUpdate();  
}

void otaLoop() {

}
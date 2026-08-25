#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <TFT_eSPI.h>

#include "config.h"
#include "config_store.h"
#include "wifi_manager.h"
#include "serial_buffer.h"
#include "screens.h"

extern TFT_eSPI tft;


// --------------------------------------------------
// OTA SCREEN
// --------------------------------------------------

static const int OTA_MAX_LINES = 10;
static const int OTA_LINE_HEIGHT = 18;
static const int OTA_LOG_X = 5;
static const int OTA_LOG_Y = 8;

static String otaLines[OTA_MAX_LINES];
static int otaLineCount = 0;

static int otaProgress = -1;


void drawOTAProgress(int progress) {

    otaProgress = progress;

    int y = tft.height() - 40;

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);

    tft.drawString(
        "Downloading: " + String(progress) + "%",
        OTA_LOG_X,
        y,
        2
    );

    // Progress bar
    int barX = OTA_LOG_X;
    int barY = tft.height() - 15;
    int barW = tft.width() - 10;
    int barH = 8;

    tft.drawRect(
        barX,
        barY,
        barW,
        barH,
        TFT_WHITE
    );

    int fillW = ((barW - 2) * progress) / 100;

    if (fillW > 0) {
        tft.fillRect(
            barX + 1,
            barY + 1,
            fillW,
            barH - 2,
            TFT_BLUE
        );
    }
}

// Redraw the complete OTA screen
static void redrawOTAScreen() {

  tft.fillScreen(TFT_BLACK);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  for (int i = 0; i < otaLineCount; i++) {
    tft.drawString(
      otaLines[i],
      OTA_LOG_X,
      OTA_LOG_Y + i * OTA_LINE_HEIGHT,
      2
    );
  }

  // Progress bar / percentage at bottom
  if (otaProgress >= 0) {

    int y = tft.height() - 35;

    tft.setTextColor(TFT_BLUE, TFT_BLACK);

    tft.drawString(
      "Downloading: " + String(otaProgress) + "%",
      OTA_LOG_X,
      y,
      2
    );

    // Progress bar
    int barX = OTA_LOG_X;
    int barY = tft.height() - 15;
    int barW = tft.width() - 10;
    int barH = 8;

    tft.drawRect(
      barX,
      barY,
      barW,
      barH,
      TFT_WHITE
    );

    int fillW = ((barW - 2) * otaProgress) / 100;

    if (fillW > 0) {
      tft.fillRect(
        barX + 1,
        barY + 1,
        fillW,
        barH - 2,
        TFT_BLUE
      );
    }
  }
}


// Start a new OTA screen
void initOTAScreen() {

  otaLineCount = 0;
  otaProgress = -1;

  for (int i = 0; i < OTA_MAX_LINES; i++) {
    otaLines[i] = "";
  }

  tft.fillScreen(TFT_BLACK);
}

// Update only the progress display
void drawOTAScreen() {
    tft.fillScreen(TFT_BLACK);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    for (int i = 0; i < otaLineCount; i++) {
        tft.drawString(
            otaLines[i],
            OTA_LOG_X,
            OTA_LOG_Y + i * OTA_LINE_HEIGHT,
            2
        );
    }
}


// Append a line to the OTA screen
void appendOTAScreen(const String& message) {

    if (otaLineCount >= OTA_MAX_LINES) {

        for (int i = 1; i < OTA_MAX_LINES; i++) {
            otaLines[i - 1] = otaLines[i];
        }

        otaLineCount = OTA_MAX_LINES - 1;
    }

    otaLines[otaLineCount++] = message;

    drawOTAScreen();
}


// --------------------------------------------------
// VERSION
// --------------------------------------------------

String baseURL() {
  return OTA_BASE_URL;
}


String sanitizeVersion(const String& raw) {

  String clean = "";

  for (size_t i = 0; i < raw.length(); i++) {

    char c = raw.charAt(i);

    if ((c >= '0' && c <= '9') || c == '.') {
      clean += c;
    }
  }

  return clean;
}


// --------------------------------------------------
// FETCH SERVER VERSION
// --------------------------------------------------

String fetchServerVersion() {

  WiFiClientSecure client;
  HTTPClient http;

  client.setInsecure();

  String url = baseURL() + "/latest.txt";

  http.begin(client, url);

  int code = http.GET();

  if (code <= 0) {

    appendOTAScreen("Retrying...");

    http.end();

    delay(200);

    http.begin(client, url);

    code = http.GET();
  }

  String ver = "";

  if (code == HTTP_CODE_OK) {

    ver = http.getString();

    ver.trim();

    ver = sanitizeVersion(ver);
  }
  else {

    appendOTAScreen(
      "Version check failed"
    );
  }

  http.end();

  return ver;
}


// --------------------------------------------------
// VERSION COMPARISON
// --------------------------------------------------

bool isNewer(
  const String& server,
  const String& local
) {

  int sMaj = 0;
  int sMin = 0;
  int sPatch = 0;

  int lMaj = 0;
  int lMin = 0;
  int lPatch = 0;

  sscanf(
    server.c_str(),
    "%d.%d.%d",
    &sMaj,
    &sMin,
    &sPatch
  );

  sscanf(
    local.c_str(),
    "%d.%d.%d",
    &lMaj,
    &lMin,
    &lPatch
  );

  if (sMaj != lMaj)
    return sMaj > lMaj;

  if (sMin != lMin)
    return sMin > lMin;

  return sPatch > lPatch;
}


// --------------------------------------------------
// OTA DOWNLOAD
// --------------------------------------------------

void doUpdate() {

  String url = baseURL() + "/firmware.bin";

  appendOTAScreen("Downloading update...");

  WiFiClientSecure client;

  client.setInsecure();

  httpUpdate.onProgress([](int cur, int total) {

    unsigned int pct = total ? (cur * 100) / total : 0;

    static int lastShownPct = -1;

    if ((int)pct != lastShownPct) {
        lastShownPct = pct;
        drawOTAProgress(pct);
    }
    
  });

  httpUpdate.rebootOnUpdate(true);

  t_httpUpdate_return ret =
    httpUpdate.update(client, url);

  switch (ret) {

    case HTTP_UPDATE_FAILED:

      appendOTAScreen(
        "Update failed"
      );

      appendOTAScreen(
        String(httpUpdate.getLastErrorString())
      );

      break;


    case HTTP_UPDATE_NO_UPDATES:

      appendOTAScreen(
        "No updates"
      );

      break;


    case HTTP_UPDATE_OK:

      // Board normally reboots automatically
      appendOTAScreen(
        "Update complete"
      );

      break;
  }
}


// --------------------------------------------------
// CHECK FOR UPDATE
// --------------------------------------------------

static void runUpdateCheck() {

  initOTAScreen();

  appendOTAScreen(
    "Checking for update..."
  );


  if (WiFi.status() != WL_CONNECTED) {

    appendOTAScreen(
      "WiFi not connected"
    );

    return;
  }


  String serverVer =
    fetchServerVersion();


  if (serverVer.length() == 0) {

    appendOTAScreen(
      "No version available"
    );

    return;
  }


  appendOTAScreen(
    "Current : " + String(BUILD_VERSION)
  );

  appendOTAScreen(
    "Server  :  " + serverVer
  );


  if (isNewer(
        serverVer,
        BUILD_VERSION)) {

    appendOTAScreen(
      "Update available"
    );

    delay(800);

    doUpdate();

  }
  else {

    appendOTAScreen(
      "Up to date"
    );
  }
}

void checkForUpdate() {
  runUpdateCheck();

  // Let the final message be readable, then hand the display back
  // to the underlying page: full clear, full redraw next pass.
  // (On successful installs the board reboots before reaching this.)
  delay(1500);
  redrawCurrentPage();
}


// --------------------------------------------------
// OTA INIT / LOOP
// --------------------------------------------------

// On-boot updates must wait for the WiFi association to complete,
// so the check is armed here and fired from otaLoop().
static bool bootCheckPending = false;
static unsigned long bootCheckArmedAt = 0;

static constexpr unsigned long OTA_BOOT_WIFI_TIMEOUT_MS = 60000;

void otaInit() {
  bootCheckPending = config.otaCheckOnStart;
  bootCheckArmedAt = millis();
}

void otaLoop() {
  if (!bootCheckPending) {
    return;
  }

  if (wifiConnected()) {
    bootCheckPending = false;
    bufferedSerialPrintln("[OTA] WiFi ready, checking for on-boot update");
    checkForUpdate();
    return;
  }

  // No connection in time: give up quietly (offline use is valid)
  if (millis() - bootCheckArmedAt > OTA_BOOT_WIFI_TIMEOUT_MS) {
    bootCheckPending = false;
    bufferedSerialPrintln("[OTA] Skipping on-boot update: no WiFi connection");
  }
}

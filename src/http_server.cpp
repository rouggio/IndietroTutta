#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <TinyGPSPlus.h>

#include "http_server.h"
#include "config_store.h"

static WebServer server(80);

static bool started = false;
static bool scanCached = false;
static int cachedNetworkCount = 0;
static unsigned long lastScanTime = 0;
static const unsigned long scanCacheMs = 30000;

static TinyGPSPlus* gpsRef = nullptr;

static int getNetworkScanCount()
{
    if (!scanCached || (millis() - lastScanTime) > scanCacheMs)
    {
        scanCached = true;
        lastScanTime = millis();
        cachedNetworkCount = WiFi.scanNetworks();
    }

    return cachedNetworkCount;
}

static void handleSetupPrompt()
{
    int n = getNetworkScanCount();
    bool haveNetworks = (n > 0);

    String html;

    html += "<!DOCTYPE html><html><head>"
            "<meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1, viewport-fit=cover'>"
            "<style>"
            "html, body { margin: 0; padding: 0; width: 100%; height: 100%; background: #0f172a; color: #f8fafc; font-family: Arial, sans-serif; }"
            "body { display: flex; align-items: center; justify-content: center; }"
            ".card { width: min(92vw, 460px); padding: 24px; box-sizing: border-box; background: #111827; border: 1px solid #334155; border-radius: 18px; box-shadow: 0 10px 30px rgba(0,0,0,0.35); }"
            "h2 { margin: 0 0 16px; font-size: 1.35rem; text-align: center; }"
            "label { display: block; font-size: 0.95rem; margin-bottom: 6px; color: #cbd5e1; }"
            "input, select { width: 100%; box-sizing: border-box; padding: 12px; margin-bottom: 14px; border-radius: 10px; border: 1px solid #475569; background: #1f2937; color: #f8fafc; font-size: 1rem; }"
            "input[type='submit'] { background: #2563eb; border: none; font-weight: 700; margin-top: 6px; }"
            "@media (max-height: 640px) { .card { padding: 18px; } h2 { margin-bottom: 10px; } input, select { padding: 10px; margin-bottom: 10px; } }"
            "</style></head><body>"
            "<div class='card'>"
            "<h2>Indietro Tutta Setup</h2>"
            "<form method='POST' action='/save'>"
            "<label>WiFi Network</label>";

    if (haveNetworks)
    {
        html += "<select name='ssid'>";
        for (int i = 0; i < n; i++)
        {
            html += "<option value='";
            html += WiFi.SSID(i);
            html += "'>";

            html += WiFi.SSID(i);
            html += " (";
            html += WiFi.RSSI(i);
            html += " dBm)";

            if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN)
                html += " 🔓";
            else
                html += " 🔒";

            html += "</option>";
        }
        html += "</select>";
    }
    else
    {
        html += "<input type='text' name='ssid' placeholder='Enter SSID'>";
    }

    html += "<label>Password</label>"
            "<input type='password' name='password' placeholder='Enter password'>"
            "<input type='submit' value='Save'>"
            "</form></div></body></html>";

    server.send(200, "text/html", html);
}

static void handleSetupSave()
{
    Config cfg = {};

    strncpy(cfg.ssid,
            server.arg("ssid").c_str(),
            sizeof(cfg.ssid) - 1);

    strncpy(cfg.password,
            server.arg("password").c_str(),
            sizeof(cfg.password) - 1);

    saveConfig(cfg);

    server.send(200,
                "text/html",
                "<h2>Configuration saved.</h2>"
                "<p>Rebooting...</p>");

    delay(1000);
    ESP.restart();
}

static void handleReset()
{
    Config emptyConfig = {};
    if (!saveConfig(emptyConfig))
    {
        Serial.println("[HTTP] Failed to clear stored Wi-Fi config");
    }
    else
    {
        Serial.println("[HTTP] Stored Wi-Fi config cleared");
    }

    server.send(200,
                "text/html",
                "<h2>Resetting configuration.</h2>"
                "<p>Rebooting...</p>");

    delay(1000);
    ESP.restart();
}

static void handleStatus()
{
    StaticJsonDocument<512> doc;
    doc["mode"] = (WiFi.getMode() == WIFI_AP) ? "Access Point" : "Station";
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = (WiFi.getMode() == WIFI_AP)
                     ? WiFi.softAPIP().toString()
                     : WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    if (gpsRef)
    {
        doc["gps"]["location"]["lat"] = gpsRef->location.isValid() ? gpsRef->location.lat() : 0.0;
        doc["gps"]["location"]["lng"] = gpsRef->location.isValid() ? gpsRef->location.lng() : 0.0;
        doc["gps"]["satellites"] = gpsRef->satellites.value();
        doc["gps"]["hdop"] = gpsRef->hdop.value();
    }


    String json;
    serializeJson(doc, json);

    server.send(200, "application/json", json);
}

static void handleHealth()
{
    server.send(200, "text/plain", "OK");
}

static void handleRedirect()
{
    server.sendHeader("Location", "http://192.168.4.1/config", true);
    server.send(302, "text/plain", "");
}

void httpServerInit(TinyGPSPlus &gps)
{
    if (started)
        return;

    started = true;

    gpsRef = &gps;

    server.on("/", HTTP_GET, handleRedirect);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/health", HTTP_GET, handleHealth);
    server.on("/config", HTTP_GET, handleSetupPrompt);
    server.on("/save", HTTP_POST, handleSetupSave);
    server.on("/reset", HTTP_POST, handleReset);
    server.onNotFound(handleRedirect);

    server.begin();
}

void httpServerLoop()
{
    server.handleClient();
}
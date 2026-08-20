#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <TinyGPSPlus.h>

#include <esp_heap_caps.h>
#include "gps_debug.h"
#include "http_server.h"
#include "config_store.h"
#include "config.h"
#include "serial_buffer.h"
#include "wifi_manager.h"

static WebServer server(80);

static bool started = false;
static bool scanCached = false;
static int cachedNetworkCount = 0;
static unsigned long lastScanTime = 0;
static const unsigned long scanCacheMs = 30000;

static TinyGPSPlus *gpsRef = nullptr;

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

    int savedCount = wifiNetworkCount();

    String html;

    html += "<!DOCTYPE html><html><head>"
            "<meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1, viewport-fit=cover'>"
            "<style>"
            "html, body { margin: 0; padding: 0; width: 100%; min-height: 100%; background: #0f172a; color: #f8fafc; font-family: Arial, sans-serif; }"
            "body { display: flex; align-items: center; justify-content: center; padding: 20px 0; }"
            ".card { width: min(92vw, 460px); padding: 24px; box-sizing: border-box; background: #111827; border: 1px solid #334155; border-radius: 18px; box-shadow: 0 10px 30px rgba(0,0,0,0.35); }"
            "h2 { margin: 0 0 16px; font-size: 1.35rem; text-align: center; }"
            "h3 { margin: 24px 0 12px; font-size: 1.05rem; color: #cbd5e1; }"
            "label { display: block; font-size: 0.95rem; margin-bottom: 6px; color: #cbd5e1; }"
            "input, select { width: 100%; box-sizing: border-box; padding: 12px; margin-bottom: 14px; border-radius: 10px; border: 1px solid #475569; background: #1f2937; color: #f8fafc; font-size: 1rem; }"
            "input[type='submit'] { background: #2563eb; border: none; font-weight: 700; margin-top: 6px; }"
            ".saved { background: #1f2937; border: 1px solid #334155; border-radius: 10px; padding: 10px 12px; margin-bottom: 8px; display: flex; align-items: center; justify-content: space-between; gap: 10px; }"
            ".saved-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }"
            ".remove { background: #991b1b; color: white; border: none; border-radius: 7px; padding: 7px 10px; font-size: 0.85rem; }"
            ".empty { color: #94a3b8; font-size: 0.9rem; }"
            ".separator { margin: 22px 0; border-top: 1px solid #334155; }"
            "@media (max-height: 640px) { .card { padding: 18px; } h2 { margin-bottom: 10px; } input, select { padding: 10px; margin-bottom: 10px; } }"
            "</style></head><body>"
            "<div class='card'>"
            "<h2>Indietro Tutta Setup</h2>"

            "<form method='POST' action='/save'>"
            "<label>WiFi Network</label>";

    // -----------------------------------------------------
    // Available networks
    // -----------------------------------------------------

    if (haveNetworks)
    {
        html += "<select name='ssid'>";

        for (int i = 0; i < n; i++)
        {
            String scannedSSID = WiFi.SSID(i);

            bool saved = false;

            for (int j = 0; j < savedCount; j++)
            {
                char savedSSID[33];

                if (wifiGetNetwork(
                        j,
                        savedSSID,
                        sizeof(savedSSID)))
                {
                    if (scannedSSID == savedSSID)
                    {
                        saved = true;
                        break;
                    }
                }
            }

            html += "<option value='";
            html += scannedSSID;
            html += "'>";

            html += scannedSSID;
            html += " (";
            html += WiFi.RSSI(i);
            html += " dBm)";

            if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN)
                html += " 🔓";
            else
                html += " 🔒";

            if (saved)
                html += " ✓";

            html += "</option>";
        }

        html += "</select>";
    }
    else
    {
        html += "<input type='text' name='ssid' placeholder='Enter SSID'>";
    }

    html += "<label>Password</label>"
            "<input type='password' name='password' placeholder='Enter password'>";

    // -----------------------------------------------------
    // Timezone
    // -----------------------------------------------------

    html += "<label>Timezone</label>"
            "<select name='timezoneOffset'>"
            "<option value='0'>UTC</option>"
            "<option value='-12'>UTC-12</option>"
            "<option value='-11'>UTC-11</option>"
            "<option value='-10'>UTC-10</option>"
            "<option value='-9'>UTC-9</option>"
            "<option value='-8'>UTC-8</option>"
            "<option value='-7'>UTC-7</option>"
            "<option value='-6'>UTC-6</option>"
            "<option value='-5'>UTC-5</option>"
            "<option value='-4'>UTC-4</option>"
            "<option value='-3'>UTC-3</option>"
            "<option value='-2'>UTC-2</option>"
            "<option value='-1'>UTC-1</option>"
            "<option value='1'>UTC+1</option>"
            "<option value='2'>UTC+2</option>"
            "<option value='3'>UTC+3</option>"
            "<option value='4'>UTC+4</option>"
            "<option value='5'>UTC+5</option>"
            "<option value='6'>UTC+6</option>"
            "<option value='7'>UTC+7</option>"
            "<option value='8'>UTC+8</option>"
            "<option value='9'>UTC+9</option>"
            "<option value='10'>UTC+10</option>"
            "<option value='11'>UTC+11</option>"
            "<option value='12'>UTC+12</option>"
            "<option value='13'>UTC+13</option>"
            "<option value='14'>UTC+14</option>"
            "</select>";

    html += "<input type='submit' value='Save'>"
            "</form>";

    // -----------------------------------------------------
    // Saved networks
    // -----------------------------------------------------

    html += "<div class='separator'></div>"
            "<h3>Saved WiFi networks</h3>";

    if (savedCount == 0)
    {
        html += "<div class='empty'>No saved networks.</div>";
    }
    else
    {
        for (int i = 0; i < savedCount; i++)
        {
            char savedSSID[33];

            if (!wifiGetNetwork(
                    i,
                    savedSSID,
                    sizeof(savedSSID)))
            {
                continue;
            }

            html += "<div class='saved'>"
                    "<span class='saved-name'>✓ ";

            html += savedSSID;

            html += "</span>"
                    "<form method='POST' action='/wifi/remove' style='margin:0;'>"
                    "<input type='hidden' name='ssid' value='";

            html += savedSSID;

            html += "'>"
                    "<button class='remove' type='submit'>Remove</button>"
                    "</form>"
                    "</div>";
        }
    }

    html += "</div></body></html>";

    server.send(200, "text/html", html);
}

static void handleSetupSave()
{
    Config cfg{};

    loadConfig(cfg);

    bool wifiSaved = false;

    if (server.hasArg("ssid") &&
        server.arg("ssid").length() > 0)
    {
        String ssid = server.arg("ssid");
        String password = "";

        if (server.hasArg("password"))
        {
            password = server.arg("password");
        }

        wifiSaved = wifiAddNetwork(
            ssid.c_str(),
            password.c_str()
        );
    }

    if (server.hasArg("timezoneOffset") &&
        server.arg("timezoneOffset").length() > 0)
    {
        cfg.timezoneOffsetHours =
            server.arg("timezoneOffset").toInt();
    }

    if (server.hasArg("otaCheckOnStart"))
    {
        cfg.otaCheckOnStart = true;
    }
    else
    {
        cfg.otaCheckOnStart = false;
    }

    if (!saveConfig(cfg))
    {
        bufferedSerialPrintln(
            "[HTTP] Failed to save configuration"
        );

        server.send(
            500,
            "text/html",
            "<h2>Configuration save failed.</h2>"
        );

        return;
    }

    // Keep global configuration synchronized
    config = cfg;

    if (server.hasArg("ssid") &&
        server.arg("ssid").length() > 0 &&
        !wifiSaved)
    {
        server.send(
            500,
            "text/html",
            "<h2>WiFi configuration failed.</h2>"
            "<p>Maximum number of networks may have been reached.</p>"
        );

        return;
    }

    server.send(
        200,
        "text/html",
        "<h2>Configuration saved.</h2>"
        "<p>Rebooting...</p>"
    );

    delay(1000);
    ESP.restart();
}

static void handleReset()
{
    wifiClearNetworks();

    Config emptyConfig{};

    if (!saveConfig(emptyConfig))
    {
        bufferedSerialPrintln(
            "[HTTP] Failed to clear stored configuration"
        );
    }
    else
    {
        bufferedSerialPrintln(
            "[HTTP] Stored configuration cleared"
        );
    }

    server.send(
        200,
        "text/html",
        "<h2>Resetting configuration.</h2>"
        "<p>Rebooting...</p>"
    );

    delay(1000);
    ESP.restart();
}

// Simple reboot endpoint (POST) to remotely restart the device
static void handleReboot()
{
    bufferedSerialPrintln("[HTTP] Reboot requested via /reboot");

    server.send(200,
                "text/html",
                "<h2>Rebooting device</h2>\n<p>Device will restart shortly.</p>");

    // small delay to allow the response to be sent
    delay(500);
    ESP.restart();
}

static void handleStatus()
{
    StaticJsonDocument<2048> doc;
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

    JsonArray nmea = doc.createNestedArray("nmea");

    int start = getNextNMEALine();
    for (int i = 0; i < MAX_NMEA_LINES; i++)
    {
        int idx = (start + i) % MAX_NMEA_LINES;

        if (!nmeaLines[idx].isEmpty())
            nmea.add(nmeaLines[idx]);
    }

    JsonObject mem = doc.createNestedObject("mem");
    mem["freeHeap"]     = ESP.getFreeHeap();
    mem["minFreeHeap"]  = ESP.getMinFreeHeap();
    mem["maxAllocHeap"] = ESP.getMaxAllocHeap();

    JsonObject sys = doc.createNestedObject("sys");
    sys["version"] = BUILD_VERSION;
    sys["otaCheckOnStart"] = config.otaCheckOnStart;

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

static void handleWiFiRemove()
{
    if (!server.hasArg("ssid") ||
        server.arg("ssid").length() == 0)
    {
        server.send(
            400,
            "text/plain",
            "Missing SSID"
        );
        return;
    }

    String ssid = server.arg("ssid");

    if (!wifiRemoveNetwork(ssid.c_str()))
    {
        server.send(
            404,
            "text/plain",
            "WiFi network not found"
        );
        return;
    }

    bufferedSerialPrint("[HTTP] Removed WiFi network: ");
    bufferedSerialPrintln(ssid);

    server.sendHeader(
        "Location",
        "/config",
        true
    );

    server.send(
        303,
        "text/plain",
        ""
    );
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
    server.on("/wifi/remove", HTTP_POST, handleWiFiRemove);
    server.on("/reset", HTTP_POST, handleReset);
    server.on("/reboot", HTTP_POST, handleReboot);

    // Expose serial buffer as plain text at /serial
    server.on("/serial", HTTP_GET, []() {
        int total = serialLinesCount();
        String out;
        out.reserve(total * 40); // heuristic reserve
        for (int i = 0; i < total; ++i) {
            out += serialLine(i);
            out += '\n';
        }
        server.send(200, "text/plain", out);
    });

    server.onNotFound(handleRedirect);

    server.begin();
}

void httpServerLoop()
{
    server.handleClient();
}
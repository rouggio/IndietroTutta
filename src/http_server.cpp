#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <TinyGPSPlus.h>

#include <esp_heap_caps.h>
#include "http_server.h"
#include "config_store.h"
#include "config.h"
#include "serial_buffer.h"
#include "wifi_manager.h"
#include "gps_mock.h"
#include "screens.h"
#include "buttons.h"
#include "server_link.h"
#include "ota.h"

#include <TFT_eSPI.h>

extern TFT_eSPI tft;

static WebServer server(80);

static bool started = false;
static bool scanCached = false;
static int cachedNetworkCount = 0;
static unsigned long lastScanTime = 0;
static const unsigned long scanCacheMs = 30000;

static TinyGPSPlus *gpsRef = nullptr;

// ---------------------------------------------------------
// Username helpers
// ---------------------------------------------------------

// Usernames must match the backend whitelist so they are safe
// to render in the browser without escaping: letters, digits,
// space, dot, underscore, dash. Max 32 chars.
static void sanitizeUsername(
    const char *input,
    char *output,
    size_t outputSize)
{
    size_t out = 0;
    bool atStart = true;

    for (const char *p = (input ? input : ""); *p != '\0'; ++p)
    {
        char c = *p;

        bool allowed = (c >= 'A' && c <= 'Z') ||
                       (c >= 'a' && c <= 'z') ||
                       (c >= '0' && c <= '9') ||
                       c == ' ' || c == '.' || c == '_' || c == '-';

        if (!allowed)
            continue;

        if (atStart && c == ' ')
            continue;

        if (out >= outputSize - 1)
            break;

        output[out++] = c;
        atStart = false;
    }

    while (out > 0 && output[out - 1] == ' ')
        out--;

    output[out] = '\0';
}

static String escapeHtml(const char *text)
{
    String escaped;

    for (const char *p = (text ? text : ""); *p != '\0'; ++p)
    {
        switch (*p)
        {
        case '&':  escaped += "&amp;";  break;
        case '<':  escaped += "&lt;";   break;
        case '>':  escaped += "&gt;";   break;
        case '"':  escaped += "&quot;"; break;
        case '\'': escaped += "&#39;";  break;
        default:   escaped += *p;       break;
        }
    }

    return escaped;
}

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
    // Device name
    // -----------------------------------------------------

    html += "<label>Device name</label>"
            "<input type='text' name='username' maxlength='32' "
            "placeholder='Shown on the map (letters, numbers, . _ -)' value='";

    html += escapeHtml(config.username);

    html += "'>";

    // -----------------------------------------------------
    // Timezone (preselects the stored value)
    // -----------------------------------------------------

    int tz = config.timezoneOffsetHours;

    html += "<label>Timezone</label>"
            "<select name='timezoneOffset'>";

    for (int off = -12; off <= 14; ++off)
    {
        if (off == 0)
        {
            html += "<option value='0'";
            if (tz == 0) html += " selected";
            html += ">UTC</option>";
            continue;
        }

        html += "<option value='" + String(off) + "'";
        if (tz == off) html += " selected";
        html += ">UTC" + String(off > 0 ? "+" : "") + String(off) + "</option>";
    }

    html += "</select>";

    // -----------------------------------------------------
    // Speed unit
    // -----------------------------------------------------

    static const char* speedUnitLabels[SPEED_UNITS] = { "kn", "km/h", "mph" };

    html += "<label>Speed unit</label>"
            "<select name='speedUnit'>";

    for (int u = 0; u < SPEED_UNITS; ++u)
    {
        html += "<option value='" + String(u) + "'";
        if (config.speedUnit == u) html += " selected";
        html += ">" + String(speedUnitLabels[u]) + "</option>";
    }

    html += "</select>";

    // -----------------------------------------------------
    // OTA on boot
    // -----------------------------------------------------

    html += "<label style='display:flex; align-items:center; gap:10px;'>"
            "<input type='checkbox' name='otaCheckOnStart' "
            "style='width:auto; margin:0;'";

    if (config.otaCheckOnStart)
        html += " checked";

    html += "> Check for OTA update on boot</label>";

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

    if (server.hasArg("speedUnit"))
    {
        int unit = server.arg("speedUnit").toInt();

        if (unit < 0 || unit >= SPEED_UNITS)
            unit = 0;

        cfg.speedUnit = unit;
    }

    // -----------------------------------------------------
    // Device name (blank keeps the current one)
    // -----------------------------------------------------

    if (server.hasArg("username"))
    {
        char sanitized[MAX_USERNAME_LEN + 1];

        sanitizeUsername(
            server.arg("username").c_str(),
            sanitized,
            sizeof(sanitized)
        );

        if (sanitized[0] != '\0')
        {
            strlcpy(cfg.username, sanitized, sizeof(cfg.username));
        }
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
// Mock GPS toggle (POST /mock?on=1|0) — scripted fixes for indoor testing.
// Uploads stay suppressed while mock is on; the race screen shows MOCK.
static void handleMock()
{
    const bool on = server.hasArg("on") && server.arg("on") != "0";
    gpsMockSet(on);
    redrawCurrentPage();
    bufferedSerialPrintln(on ? "[HTTP] mock GPS on" : "[HTTP] mock GPS off");
    server.send(200, "application/json",
                String("{\"mock\":") + (on ? "true" : "false") + "}");
}

// Server select (GET /server → current; POST /server?mode=prod|dev&host=<ip:port>).
// Dev mode with a host talks plain HTTP to the LAN backend (local :3000 serves
// public/ota/, so OTA follows the same switch). Prod (or dev with no host)
// stays on the TLS Render endpoint.
static void handleServer()
{
    if (server.method() == HTTP_POST) {
        if (server.hasArg("mode")) {
            const String m = server.arg("mode");
            serverSetMode((m == "dev" || m == "1") ? 1 : 0);
        }
        if (server.hasArg("host")) {
            String h = server.arg("host");
            h.trim();
            if (h.startsWith("http://")) h = h.substring(7);
            if (h.startsWith("https://")) h = h.substring(8);
            while (h.endsWith("/")) h.remove(h.length() - 1);
            serverSetDevHost(h);
        }
        bufferedSerialPrintln(String("[HTTP] server -> ") + serverBaseUrl());
        redrawCurrentPage();
    }
    const int mode = serverMode();
    const String host = serverDevHost();
    String body = String("{\"mode\":\"") + (mode == 1 ? "dev" : "prod") +
                  "\",\"host\":\"" + host + "\",\"base\":\"" + serverBaseUrl() + "\"}";
    server.send(200, "application/json", body);
}

// Immediate OTA check (POST /ota): same as CONFIG LL — compares
// latest.txt on the CURRENT server (dev :3000 or prod) and pulls
// firmware.bin when newer. Takes over the screen; board reboots on OK.
static void handleOta()
{
    server.send(202, "application/json",
                String("{\"ota\":\"started\",\"server\":\"") + serverBaseUrl() + "\"}");
    delay(100); // let the response flush before the blocking download
    bufferedSerialPrintln(String("[HTTP] OTA check vs ") + serverBaseUrl());
    checkForUpdate();
}

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

    JsonObject mem = doc.createNestedObject("mem");
    mem["freeHeap"]     = ESP.getFreeHeap();
    mem["minFreeHeap"]  = ESP.getMinFreeHeap();
    mem["maxAllocHeap"] = ESP.getMaxAllocHeap();

    JsonObject sys = doc.createNestedObject("sys");
    sys["version"] = BUILD_VERSION;
    sys["otaCheckOnStart"] = config.otaCheckOnStart;
    sys["username"] = config.username;
    sys["mock"] = gpsMockActive();
    sys["server"] = serverBaseUrl();

    String json;
    serializeJson(doc, json);

    server.send(200, "application/json", json);
}

static void handleHealth()
{
    server.send(200, "text/plain", "OK");
}

// Remote button driver (POST /btn?b=L|R&e=R|RR): injects straight into
// screenButtonEvent, the same entry the physical buttons use. Lets scripts
// walk the whole UI over LAN — no finger needed.
static void handleBtn()
{
    const String b = server.hasArg("b") ? server.arg("b") : "";
    const String e = server.hasArg("e") ? server.arg("e") : "R";
    Button btn;
    if (b == "L" || b == "l") btn = Button::Left;
    else if (b == "R" || b == "r") btn = Button::Right;
    else {
        server.send(400, "text/plain", "b must be L|R");
        return;
    }
    ButtonEvent ev;
    if (e == "R" || e == "r") ev = ButtonEvent::ShortPress;
    else if (e == "RR" || e == "rr") ev = ButtonEvent::LongPress;
    else {
        server.send(400, "text/plain", "e must be R|RR");
        return;
    }
    screenButtonEvent(btn, ev);
    bufferedSerialPrintln(String("[HTTP] btn ") + b + " " + e);
    server.send(200, "application/json",
                String("{\"b\":\"") + b + "\",\"e\":\"" + e + "\"}");
}

// Framebuffer grab (GET /screen): raw RGB565 big-endian, 320x240, no header.
// scripts/grab_screen.py turns it into a PNG. Slow (SPI reads, seconds) —
// a debugging tool, not a live feed.
static void handleScreen()
{
    const int w = tft.width();
    const int h = tft.height();
    if (w <= 0 || h <= 0 || w > 480 || h > 480) {
        server.send(500, "text/plain", "bad geometry");
        return;
    }
    server.setContentLength((size_t)w * h * 2);
    server.send(200, "application/octet-stream", "");
    static uint16_t row[480];
    for (int y = 0; y < h; y++) {
        const int n = w > 480 ? 480 : w;
        for (int x = 0; x < n; x++) row[x] = tft.readPixel(x, y);
        server.sendContent((const char*)row, (size_t)n * 2);
        if ((y & 15) == 15) delay(1); // feed the watchdog on slow panels
    }
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
    server.on("/mock", HTTP_POST, handleMock);
    server.on("/ota", HTTP_POST, handleOta);
    server.on("/server", HTTP_GET, handleServer);
    server.on("/server", HTTP_POST, handleServer);
    server.on("/btn", HTTP_POST, handleBtn);
    server.on("/screen", HTTP_GET, handleScreen);

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
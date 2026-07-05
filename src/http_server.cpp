#include <WebServer.h>
#include <WiFi.h>

#include "http_server.h"
#include "config_store.h"

static WebServer server(80);

static bool started = false;

static void handleSetupPrompt()
{
    int n = WiFi.scanNetworks();

    String html;

    html += "<!DOCTYPE html><html><body>";
    html += "<h2>Indietro Tutta Setup</h2>";

    html += "<form method='POST' action='/save'>";

    html += "WiFi Network<br>";
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
    html += "</select><br><br>";

    html += "Password<br>";
    html += "<input type='password' name='password'><br><br>";

    html += "<input type='submit' value='Save'>";

    html += "</form></body></html>";

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

static void handleStatus()
{
    String html;

    html += "<!DOCTYPE html><html><body>";
    html += "<h2>Indietro Tutta</h2>";

    html += "<table border='1' cellpadding='5'>";

    html += "<tr><td>Mode</td><td>";
    html += (WiFi.getMode() == WIFI_AP) ? "Access Point" : "Station";
    html += "</td></tr>";

    html += "<tr><td>SSID</td><td>";
    html += WiFi.SSID();
    html += "</td></tr>";

    html += "<tr><td>IP</td><td>";
    html += (WiFi.getMode() == WIFI_AP)
              ? WiFi.softAPIP().toString()
              : WiFi.localIP().toString();
    html += "</td></tr>";

    html += "<tr><td>RSSI</td><td>";
    html += String(WiFi.RSSI());
    html += " dBm</td></tr>";

    html += "</table><br>";

    html += "<a href='/config'>Configuration</a>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}

static void handleHealth()
{
    server.send(200, "text/plain", "OK");
}

void httpServerInit()
{
    if (started)
        return;

    started = true;

    server.on("/", HTTP_GET, handleStatus);
    server.on("/health", HTTP_GET, handleHealth);
    server.on("/config", HTTP_GET, handleSetupPrompt);
    server.on("/save", HTTP_POST, handleSetupSave);

    server.begin();
}

void httpServerLoop()
{
    server.handleClient();
}
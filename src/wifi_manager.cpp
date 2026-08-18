#include "wifi_manager.h"
#include "config_store.h"
#include "http_server.h"
#include "serial_buffer.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <TinyGPSPlus.h>

DNSServer dns;
static bool wifiConfigured = false;
static bool wifiConnectedFlag = false;
static unsigned long lastRetry = 0;
static const unsigned int maxConnectionAttempts = 5;
static unsigned int connectionAttempts = 0;
static bool connectionRetryExhausted = false;

static void startAccessPoint()
{
    WiFi.mode(WIFI_AP_STA);

    if (!WiFi.softAP("IndietroTutta"))
    {
        bufferedSerialPrintln("[WiFi] Failed to start AP");
        return;
    }

    bufferedSerialPrint("[WiFi] AP IP: ");
    bufferedSerialPrintln(WiFi.softAPIP().toString());

    dns.start(53, "*", WiFi.softAPIP());
}

static void startStation()
{
    if (!wifiConfigured)
        return;

    bufferedSerialPrint("[WiFi] Connecting to ");
    bufferedSerialPrintln(String(config.ssid));

    WiFi.begin(config.ssid, config.password);
}

void wifiInit(TinyGPSPlus &gps)
{
    bufferedSerialPrintln("[WiFi] Initializing");

    memset(&config, 0, sizeof(config));
    wifiConfigured = loadConfig(config);
    wifiConfigured = wifiConfigured && config.ssid[0] != '\0' && config.password[0] != '\0';
    connectionAttempts = 0;
    connectionRetryExhausted = false;

    startAccessPoint();

    if (wifiConfigured && config.ssid[0] != '\0')
    {
        startStation();
    }

    httpServerInit(gps);
}

void wifiLoop()
{
    dns.processNextRequest();
    httpServerLoop();

    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED)
    {
        if (!wifiConnectedFlag)
        {
            wifiConnectedFlag = true;
            connectionAttempts = 0;
            connectionRetryExhausted = false;

            bufferedSerialPrintln("[WiFi] Connected");
            bufferedSerialPrint("[WiFi] STA IP: ");
            bufferedSerialPrintln(WiFi.localIP().toString());
        }

        return;
    }

    wifiConnectedFlag = false;

    if (!wifiConfigured || connectionRetryExhausted)
        return;

    if (millis() - lastRetry > 10000)
    {
        lastRetry = millis();
        connectionAttempts++;

        bufferedSerialPrintln(String("[WiFi] Retry attempt ") + String(connectionAttempts) + String(" of ") + String(maxConnectionAttempts));

        if (connectionAttempts >= maxConnectionAttempts)
        {
            bufferedSerialPrintln("[WiFi] Max retries reached; clearing saved SSID and password");
            memset(config.ssid, 0, sizeof(config.ssid));
            memset(config.password, 0, sizeof(config.password));

            if (!saveConfig(config))
            {
                bufferedSerialPrintln("[WiFi] Failed to persist cleared credentials");
            }
            else
            {
                bufferedSerialPrintln("[WiFi] Saved SSID and password cleared");
            }

            wifiConfigured = false;
            connectionRetryExhausted = true;
            return;
        }

        WiFi.disconnect();
        WiFi.begin(config.ssid, config.password);
    }
}

bool wifiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}
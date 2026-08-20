#include "wifi_manager.h"
#include "config_store.h"
#include "http_server.h"
#include "serial_buffer.h"

#include <WiFi.h>
#include <WiFiMulti.h>
#include <DNSServer.h>
#include <TinyGPSPlus.h>

DNSServer dns;
WiFiMulti wifiMulti;

static bool wifiConnectedFlag = false;
static unsigned long lastRetry = 0;

static const unsigned long wifiRetryInterval = 10000;

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

// ---------------------------------------------------------
// Load all saved networks into WiFiMulti
// ---------------------------------------------------------

static void loadSavedNetworks()
{
    int count = wifiNetworkCount();

    bufferedSerialPrint("[WiFi] Saved networks: ");
    bufferedSerialPrintln(String(count));

    for (int i = 0; i < count; i++)
    {
        char ssid[33];
        char password[65];

        if (!loadWiFiNetwork(
                i,
                ssid,
                sizeof(ssid),
                password,
                sizeof(password)))
        {
            bufferedSerialPrint("[WiFi] Failed to load network #");
            bufferedSerialPrintln(String(i));
            continue;
        }

        bufferedSerialPrint("[WiFi] Adding network: ");
        bufferedSerialPrintln(ssid);

        wifiMulti.addAP(ssid, password);
    }
}

// ---------------------------------------------------------
// Add/update network
// ---------------------------------------------------------
bool wifiAddNetwork(
    const char* ssid,
    const char* password)
{
    if (!ssid || !password)
        return false;

    if (ssid[0] == '\0')
        return false;

    int count = wifiNetworkCount();

    // -----------------------------------------------------
    // Check if SSID already exists
    // -----------------------------------------------------

    for (int i = 0; i < count; i++)
    {
        char existingSSID[33];
        char existingPassword[65];

        if (!loadWiFiNetwork(
                i,
                existingSSID,
                sizeof(existingSSID),
                existingPassword,
                sizeof(existingPassword)))
        {
            continue;
        }

        if (strcmp(existingSSID, ssid) == 0)
        {
            bufferedSerialPrint("[WiFi] Network already exists: ");
            bufferedSerialPrintln(ssid);

            // Empty password means:
            // keep the currently stored password.
            if (password[0] == '\0')
            {
                bufferedSerialPrintln(
                    "[WiFi] Password unchanged"
                );

                // Re-register the existing credentials
                // with WiFiMulti.
                wifiMulti.addAP(
                    existingSSID,
                    existingPassword
                );

                return true;
            }

            // Non-empty password means update it.
            bufferedSerialPrintln(
                "[WiFi] Updating password"
            );

            if (!saveWiFiNetwork(
                    i,
                    ssid,
                    password))
            {
                bufferedSerialPrintln(
                    "[WiFi] Failed to update network"
                );

                return false;
            }

            wifiMulti.addAP(
                ssid,
                password
            );

            return true;
        }
    }

    // -----------------------------------------------------
    // New network
    // -----------------------------------------------------

    if (count >= MAX_WIFI_NETWORKS)
    {
        bufferedSerialPrintln(
            "[WiFi] Maximum number of saved networks reached"
        );

        return false;
    }

    bufferedSerialPrint("[WiFi] Adding network: ");
    bufferedSerialPrintln(ssid);

    if (!saveWiFiNetwork(
            count,
            ssid,
            password))
    {
        bufferedSerialPrintln(
            "[WiFi] Failed to save network"
        );

        return false;
    }

    wifiMulti.addAP(
        ssid,
        password
    );

    return true;
}

// ---------------------------------------------------------

bool wifiRemoveNetwork(const char* ssid)
{
    if (!ssid)
        return false;

    int count = wifiNetworkCount();

    for (int i = 0; i < count; i++)
    {
        char existingSSID[33];
        char existingPassword[65];

        if (!loadWiFiNetwork(
                i,
                existingSSID,
                sizeof(existingSSID),
                existingPassword,
                sizeof(existingPassword)))
        {
            continue;
        }

        if (strcmp(existingSSID, ssid) == 0)
        {
            bufferedSerialPrint("[WiFi] Removing network: ");
            bufferedSerialPrintln(ssid);

            return deleteWiFiNetwork(i);
        }
    }

    return false;
}

// ---------------------------------------------------------

void wifiClearNetworks()
{
    clearWiFiNetworks();

    bufferedSerialPrintln("[WiFi] All saved networks cleared");
}

// ---------------------------------------------------------

bool wifiGetNetwork(
    int index,
    char* ssid,
    size_t ssidSize)
{
    if (!ssid || ssidSize == 0)
        return false;

    char password[65];

    return loadWiFiNetwork(
        index,
        ssid,
        ssidSize,
        password,
        sizeof(password)
    );
}

// ---------------------------------------------------------

void wifiInit(TinyGPSPlus& gps)
{
    bufferedSerialPrintln("[WiFi] Initializing");

    Config loadedConfig{};

    if (loadConfig(loadedConfig))
    {
        config = loadedConfig;

        bufferedSerialPrintln("[WiFi] Configuration loaded");
    }
    else
    {
        config = {};

        bufferedSerialPrintln(
            "[WiFi] No valid configuration found"
        );
    }

    wifiConnectedFlag = false;
    lastRetry = 0;

    startAccessPoint();

    loadSavedNetworks();

    if (wifiNetworkCount() > 0)
    {
        bufferedSerialPrintln(
            "[WiFi] Attempting connection..."
        );

        wifiMulti.run(5000);
    }
    else
    {
        bufferedSerialPrintln(
            "[WiFi] No saved networks"
        );
    }

    httpServerInit(gps);
}

// ---------------------------------------------------------

void wifiLoop()
{
    dns.processNextRequest();
    httpServerLoop();

    if (wifiNetworkCount() == 0)
        return;

    wl_status_t status = WiFi.status();

    // -----------------------------------------------------
    // Connected
    // -----------------------------------------------------

    if (status == WL_CONNECTED)
    {
        if (!wifiConnectedFlag)
        {
            wifiConnectedFlag = true;

            bufferedSerialPrintln("[WiFi] Connected");

            bufferedSerialPrint("[WiFi] SSID: ");
            bufferedSerialPrintln(WiFi.SSID());

            bufferedSerialPrint("[WiFi] STA IP: ");
            bufferedSerialPrintln(
                WiFi.localIP().toString()
            );
        }

        return;
    }

    // -----------------------------------------------------
    // Connection lost
    // -----------------------------------------------------

    if (wifiConnectedFlag)
    {
        wifiConnectedFlag = false;

        bufferedSerialPrintln(
            "[WiFi] Connection lost"
        );

        bufferedSerialPrintln(
            "[WiFi] Searching for another saved network..."
        );
    }

    // -----------------------------------------------------
    // Let WiFiMulti find another available network
    // -----------------------------------------------------

    if (millis() - lastRetry >= 1000)
    {
        lastRetry = millis();

        uint8_t result = wifiMulti.run(5000);

        if (result == WL_CONNECTED)
        {
            bufferedSerialPrintln(
                "[WiFi] Switched to saved network"
            );

            bufferedSerialPrint("[WiFi] SSID: ");
            bufferedSerialPrintln(WiFi.SSID());

            bufferedSerialPrint("[WiFi] STA IP: ");
            bufferedSerialPrintln(
                WiFi.localIP().toString()
            );
        }
    }
}

// ---------------------------------------------------------

bool wifiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}
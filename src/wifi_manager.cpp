#include "wifi_manager.h"
#include "config_store.h"
#include "http_server.h"
#include "serial_buffer.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <TinyGPSPlus.h>

DNSServer dns;

// ---------------------------------------------------------
// Non-blocking reconnection state machine.
//
// We never call blocking connect APIs from the main loop:
// wifiLoop() starts association attempts with WiFi.begin()
// (which returns immediately) and just polls WiFi.status().
// A candidate that has not associated within
// WIFI_ATTEMPT_TIMEOUT_MS is considered failed and the next
// saved credential is tried on a following pass.
// ---------------------------------------------------------

static bool wifiConnectedFlag = false;
static int retryIndex = -1;
static unsigned long attemptStartedAt = 0;

static const unsigned long WIFI_ATTEMPT_TIMEOUT_MS = 5000;

// ---------------------------------------------------------
// Start associating with the next saved network.
// Returns false when there is nothing to try.
// ---------------------------------------------------------

static bool beginNextNetwork()
{
    int count = wifiNetworkCount();

    if (count <= 0)
        return false;

    retryIndex = (retryIndex + 1) % count;

    char ssid[33];
    char password[65];

    if (!loadWiFiNetwork(
            retryIndex,
            ssid,
            sizeof(ssid),
            password,
            sizeof(password)))
    {
        // Broken slot: let the timeout move us to the next one
        attemptStartedAt = millis();
        return false;
    }

    bufferedSerialPrint("[WiFi] Trying: ");
    bufferedSerialPrintln(ssid);

    // Non-blocking: returns immediately
    WiFi.begin(ssid, password);

    attemptStartedAt = millis();

    return true;
}

// Point the rotation at a specific saved index so it is tried
// on the next pass (used when a network is added/updated).
static void preferNetwork(int index)
{
    retryIndex = index - 1;
    attemptStartedAt = 0;
}

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

                // Try this network on the next pass
                preferNetwork(i);

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

            preferNetwork(i);

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

    preferNetwork(count);

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

            bool removed = deleteWiFiNetwork(i);

            // Indices shifted: restart the rotation cleanly
            retryIndex = -1;

            return removed;
        }
    }

    return false;
}

// ---------------------------------------------------------

void wifiClearNetworks()
{
    clearWiFiNetworks();

    retryIndex = -1;

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
    retryIndex = -1;
    attemptStartedAt = 0;

    startAccessPoint();

    loadSavedNetworks();

    if (wifiNetworkCount() > 0)
    {
        bufferedSerialPrintln(
            "[WiFi] Attempting connection..."
        );

        // Non-blocking: association continues while the
        // splash screen and main loop run
        beginNextNetwork();
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
    // Rotate to the next saved credential once the current
    // attempt has run out of time. WiFi.begin() is
    // non-blocking, so this never stalls the main loop.
    // -----------------------------------------------------

    if (millis() - attemptStartedAt >= WIFI_ATTEMPT_TIMEOUT_MS)
    {
        beginNextNetwork();
    }
}

// ---------------------------------------------------------

bool wifiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}
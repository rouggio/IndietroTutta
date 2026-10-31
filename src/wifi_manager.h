#pragma once

#include <TinyGPSPlus.h>
#include <stddef.h>

void wifiInit(TinyGPSPlus& gps);
void wifiLoop();

bool wifiConnected();

// Unique AP name for this board: "IndietroTutta-XXXX", XXXX = last two bytes
// of the STA MAC (also the backend's DeviceId), so two devices in range never
// collide. Returns "IndietroTutta" until the MAC is readable.
const char* wifiApSsid();

// WiFi network management
bool wifiAddNetwork(const char* ssid, const char* password);
bool wifiRemoveNetwork(const char* ssid);
void wifiClearNetworks();

bool wifiGetNetwork(
    int index,
    char* ssid,
    size_t ssidSize
);
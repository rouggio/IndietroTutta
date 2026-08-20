#pragma once

#include <TinyGPSPlus.h>
#include <stddef.h>

void wifiInit(TinyGPSPlus& gps);
void wifiLoop();

bool wifiConnected();

// WiFi network management
bool wifiAddNetwork(const char* ssid, const char* password);
bool wifiRemoveNetwork(const char* ssid);
void wifiClearNetworks();

bool wifiGetNetwork(
    int index,
    char* ssid,
    size_t ssidSize
);
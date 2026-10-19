#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Effective server: prod Render, or a dev LAN host picked on the portal.
// Mode + host live as separate NVS keys (never in the cfg blob, so no
// migration can wipe the provisioned config).
int serverMode();          // 0 = prod, 1 = dev
String serverDevHost();    // e.g. "192.168.0.2:3000", may be empty
void serverSetMode(int mode);
void serverSetDevHost(const String& host);
// Mode switch as a user performs it (CONFIG row, portal /server). PROD also
// forces the fix source back to the real GPS: the mock feed is a dev tool
// whose scripted runs live on the laptop backend, so MOCK in PROD would leave
// the boat with no fix at all. Both switch paths go through here.
void serverApplyMode(int mode);
String serverBaseUrl();    // https://prod or http://<devHost>
bool serverUseTLS();       // false in dev mode with a host set

// One object per request; owns the right transport for the current mode.
// Usage: ServerLink link; if (link.begin(url)) { link.http.GET(); ... link.http.end(); }
// (doUpdate needs the raw client: link.tls / link.plain with httpUpdate.)
// 8s timeout: a hung socket must never stall the backend task (which would
// delay every later upload and spike displayed data age).
struct ServerLink {
    WiFiClient plain;
    WiFiClientSecure tls;
    HTTPClient http;
    ServerLink() { tls.setInsecure(); }
    bool begin(const String& url) {
        const bool ok = serverUseTLS() ? http.begin(tls, url) : http.begin(plain, url);
        if (ok) http.setTimeout(8000);
        return ok;
    }
};

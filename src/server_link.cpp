#include "server_link.h"
#include "config.h"

#include <Preferences.h>

int serverMode()
{
    Preferences prefs;
    if (!prefs.begin("srv", true)) return 0;
    const int m = prefs.getInt("mode", 0);
    prefs.end();
    return (m == 1) ? 1 : 0;
}

String serverDevHost()
{
    Preferences prefs;
    if (!prefs.begin("srv", true)) return String();
    const String h = prefs.getString("host", "");
    prefs.end();
    return h;
}

void serverSetMode(int mode)
{
    Preferences prefs;
    if (!prefs.begin("srv", false)) return;
    prefs.putInt("mode", (mode == 1) ? 1 : 0);
    prefs.end();
}

void serverApplyMode(int mode)
{
    serverSetMode(mode);
    // A mode switch does NOT touch the fix source any more. It used to force
    // REAL on PROD because the FX row was hidden there, so a boat switched to
    // PROD with MOCK on had no fix at all — but that also silently reverted a
    // choice the skipper had just made on the panel (and it is what turned
    // mock racing against the cloud into a portal-only trick). The FX row is
    // offered in both modes now, so the source is the skipper's to set and it
    // is visible on screen when it is MOCK.
}

void serverSetDevHost(const String& host)
{
    Preferences prefs;
    if (!prefs.begin("srv", false)) return;
    prefs.putString("host", host);
    prefs.end();
}

bool serverUseTLS()
{
    // Dev mode with a host set = plain LAN HTTP. Anything else (prod, or
    // dev with no host yet) stays on the TLS prod endpoint.
    return !(serverMode() == 1 && serverDevHost().length() > 0);
}

String serverBaseUrl()
{
    if (!serverUseTLS()) return String("http://") + serverDevHost();
    return String(BASE_URL);
}

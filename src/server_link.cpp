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

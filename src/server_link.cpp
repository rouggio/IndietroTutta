#include "server_link.h"
#include "config.h"
#include "gps_mock.h"
#include "serial_buffer.h"

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
    // PROD is a real deployment and the mock fix feed is a dev tool: its
    // scripted run comes from the laptop backend, and the FX row that turns it
    // on is not even offered in PROD. Switching modes therefore settles the
    // pair from either entry point (CONFIG row or portal /server) — a boat
    // left on MOCK in PROD has no fix at all, real or scripted.
    if (mode == 0 && gpsMockActive()) {
        gpsMockSet(false);
        bufferedSerialPrintln("[SERVER] PROD -> FX source forced to REAL");
    }
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

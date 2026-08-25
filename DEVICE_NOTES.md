# IndietroTutta — Device Firmware Notes

Codebase overview for future reference. Generated from source inspection on
2026-08-23, updated after the 2026-08 UI rework — verify against source if the
code has changed since.

## What it is

A portable ESP32 marine GPS instrument: shows speed in knots and course on a
TFT, tracks GPS position to a cloud backend, manages flagged waypoints and a
chronograph on dedicated screens, and self-updates over WiFi.
"Indietro Tutta" is an Italian racing shout ("everyone backwards / reverse all").

## Hardware

| Part | Detail |
|---|---|
| MCU | ESP32 DevKit (`esp32dev` board, Arduino framework) |
| Display | ST7789 240×320 TFT over SPI — CS 5, DC 2, RST 4, MOSI 23, SCLK 18; landscape rotation 3; BGR color order forced by build flag in `platformio.ini` |
| Buttons | GPIO21 = Left, GPIO22 = Right, active-low with pullups; 50 ms debounce, 500 ms long-press threshold |
| GPS | UART2, RX 16 / TX 17 @ 9600 baud, parsed by TinyGPSPlus |

Display config lives in `include/User_Setup.h`, force-included via
`-DUSER_SETUP_LOADED -include include/User_Setup.h` in `platformio.ini`.

## Module map (`src/`)

Single cooperative superloop in `main.cpp` — no RTOS tasks anywhere.
Loop order: serial buffer → buttons → GPS parse → screen draw → WiFi/DNS/web
server → backend.

| Module | Role |
|---|---|
| `screens.cpp` | Owns global `TFT_eSPI`; page router (`ScreenPage` enum); 200 ms redraw throttle; L-short cycle across the four top-level pages; `setCurrentPage()` clears the display to avoid stale pixels |
| `screen_one.cpp` | MAIN instrument: big speed, course/cardinal, status tiles (WiFi/cloud/GPS fix), satellite count, hint bar. No submodes. |
| `screen_waypoints.cpp` | WAYPOINTS screen: flag current position (L-long), cycle/delete markers, per-marker elapsed timer |
| `screen_timers.cpp` | TIMERS chronograph: start/stop/lap/clear, big time readout, last-four lap list |
| `screen_two.cpp` | DIAGNOSTICS page (GPS stats, IP, version) + menu: check OTA now, OTA-on-boot toggle, reboot |
| `splash_screen.cpp` | Static title, blocking `delay(2000)` |
| `buttons.cpp` | Polling driver emitting Press/LongPress/Release events to one callback |
| `backend.cpp` | Health GET every 30 s; position POST every 40 s when fix is valid; immediate flagged-position POST on demand |
| `ota.cpp` | Version check vs `latest.txt` (semver via sscanf), HTTPS firmware download with on-screen progress bar and log |
| `wifi_manager.cpp` | AP+STA management; add/remove/list saved networks; non-blocking reconnect state machine (rotates credentials with `WiFi.begin()`, polls `WiFi.status()`, no blocking calls in the loop) |
| `http_server.cpp` | On-device WebServer (port 80): provisioning portal + diagnostics endpoints |
| `config_store.cpp` | NVS persistence (`Preferences`, namespace `"wifi"`) |
| `gps.cpp` / `gps_debug.cpp` | UART ingest into TinyGPSPlus + 30-line NMEA ring buffer |
| `serial_buffer.cpp` | 200-line log capture so `/serial` can mirror the device log |

The map feature (device RGB565 screen + backend renderer `/map/*`) was removed
in 2026-08.

## Backend communication

Base URL compiled in (`src/config.h`): `https://indietrotutta.onrender.com`.

| Call | Direction | When | Notes |
|---|---|---|---|
| `GET {BASE}/health` | out | every 30 s | Headers `DeviceId: <WiFi MAC>` + `Username: <name>` ⇒ sets online flag and re-registers the device |
| `POST {BASE}/gps` | out | every 40 s with valid fix | JSON: lat/lon (7 dp), speed (kn), course, altitude, sats, flagged, username |
| Flagged post | out | L-long on WAYPOINTS screen | Same payload with `"flagged": true`; marker kept locally (max 10, FIFO) |

No authentication: identity is the `DeviceId` MAC header; the username is a
display label attached to that identity on the backend (whitelist-sanitized on
both sides). All TLS uses `WiFiClientSecure::setInsecure()` everywhere
(backend, OTA).

## Device web portal (port 80)

Device always boots as open softAP **"IndietroTutta"** plus STA; DNS server
points all names at `192.168.4.1`. Joining the AP and opening any URL redirects
to `/config`. Routes (`src/http_server.cpp`):

| Route | Purpose |
|---|---|
| `/config` | Provisioning page: WiFi scan dropdown (RSSI, lock icon), password, device name field (shown on the backend map/browser view), manual SSID fallback, timezone select, OTA-on-boot checkbox, saved-network list with remove buttons |
| `/save` | Persists SSID/password/timezone/device name/OTA flag, then reboots after 1 s |
| `/wifi/remove` | Removes one saved network |
| `/reset` | Clears ALL WiFi networks and wipes stored config, reboots |
| `/reboot` | Remote restart |
| `/status` | JSON diagnostics: WiFi state, GPS data, last 30 NMEA lines, heap, version, username |
| `/health` | Plain-text liveness |
| `/serial` | Dumps captured serial log (200 lines) |

No route requires authentication — anyone on the LAN or device AP can wipe or
reboot the device.

## OTA updates

- Server: `{OTA_BASE_URL}` = `https://indietrotutta.onrender.com/ota`
- Check: `GET latest.txt`, trimmed to digits/dots, compared as semver
- Download: `HTTPUpdate` of `firmware.bin`, reboot on success, progress bar on TFT
- Triggers: at boot if `config.otaCheckOnStart` is set, or manually from the
  diagnostics menu
- Release pipeline: `make dist` bumps `BUILD_VERSION` in `src/config.h`,
  rebuilds, copies `firmware.bin` + writes `latest.txt` into the sibling
  backend repo's `public/ota/`, commits and pushes — devices then self-update

## UI navigation

Four top-level pages cycled with **left-short**:
MAIN → DIAGNOSTICS → WAYPOINTS → TIMERS → MAIN

Grammar:

- **Left-short** — leave any submode/menu, else advance to the next page
- **Right-short** — contextual: advance selection inside menus/lists;
  start/stop on TIMERS
- **Left-long** — open/close contextual menus; FLAG position on WAYPOINTS;
  clear-all on TIMERS
- **Right-long** — run the selected entry (menus), delete waypoint (WAYPOINTS),
  record lap (TIMERS)

Per screen:

| Screen | Actions |
|---|---|
| MAIN | L-short next page. Speed/course/status only, hint bar at the bottom |
| DIAGNOSTICS | L-long menu (OTA check / OTA-on-boot toggle / reboot); R-short select; R-long execute; L-short exit/back |
| WAYPOINTS | L-long flag here · R-short cycle waypoints · R-long delete shown · elapsed timer per marker (max 10, FIFO) |
| TIMERS | R-short start/stop · R-long lap (while running) · L-long clear all · shows big MM:SS(.cc) + last 4 laps |

Every page switch fills the display black before redrawing (no stale pixels).

## Persistence

NVS (`Preferences`, namespace `"wifi"`):

- Blob key `cfg`: `Config { username[33], timezoneOffsetHours, otaCheckOnStart }`
  (size-checked load; mismatched blobs are deleted and zeroed — a firmware
  update that changes the struct resets timezone/OTA flag once; WiFi networks
  are stored under separate keys and survive)
- Per-network keys `wifi_%d_ssid` / `wifi_%d_pass` plus `wifi_count`, max 10

Compiled in (`src/config.h`): `BASE_URL`, `OTA_BASE_URL`, `BUILD_VERSION`.
Pins are hardcoded in `include/User_Setup.h` and module sources.

Waypoints and chronograph laps live in RAM only — they are lost on reboot.

## Known quirks / gotchas

- Blank device name in the portal keeps the stored username (only non-empty
  sanitized values overwrite it)
- Usernames are restricted to `[A-Za-z0-9 ._-]`, max 32 chars, on both device
  and backend, so they are safe to render unescaped in the browser
- WiFi reconnection is fully non-blocking: a credential that fails to
  associate within 5 s is rotated out on the next loop pass, so the UI/GPS
  never stall while searching for networks
- Splash is non-blocking: drawn at boot, it stays while setup runs and is
  released by `endSplash()` as soon as the main loop is ready (500 ms minimum)
- OTA download blocks the loop until finished
- All web routes unauthenticated; TLS validation disabled everywhere
- Backend "online" tile reflects only the last health/post result
- Backend device registry (`store/deviceStore.js`) and GPS points are
  in-memory — a backend restart loses usernames until the next health poll
  (~30 s) or GPS post re-registers the device

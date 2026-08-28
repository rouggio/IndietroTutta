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
| `screens.cpp` | Owns global `TFT_eSPI`; page router (`ScreenPage` enum); 200 ms redraw throttle; L-short cycles across the three top-level pages (DIAGNOSTICS excluded, RR-only); `setCurrentPage()`/`redrawCurrentPage()` full-clear helpers |
| `screen_speed.cpp` | MAIN instrument: big speed (unit-selectable kn/km/h via `config.speedUnit`), course/cardinal, status tiles (WiFi/cloud/GPS fix), satellite count, hint bar. No submodes. Readout cell is cleared before each draw so the shorter "---" placeholder never leaves ghost pixels. |
| `screen_waypoints.cpp` | WAYPOINTS screen: flag current position (L-long), cycle/delete markers, per-marker elapsed timer |
| `screen_timers.cpp` | TIMERS chronograph: start/stop (R), lap while running / reset when stopped (RR), big time readout, laps in a right-hand column; status/time lines consciously cleared to avoid ghosting |
| `screen_two.cpp` | DIAGNOSTICS page: two-column layout (GPS | SYSTEM), title font consistent with other screens |
| `screen_config.cpp` | CONFIG page: WiFi SSID/IP status, selectable rows — OTA-on-boot toggle, speed unit cycle (R select, RR apply); LL runs an immediate OTA check |
| `splash_screen.cpp` | Static title, non-blocking (held by `screens.cpp` until setup completes) |
| `screen_serial.cpp` | **Deleted** — an unused, dangling standalone TFT instance; serial capture is served via `/serial` from `serial_buffer.cpp` instead |
| `buttons.cpp` | Polling driver emitting Press/LongPress/Release events to one callback |
| `backend.cpp` | Dedicated FreeRTOS task owns ALL network I/O: health GET every 30 s plus a 4-deep work queue of position POSTs; the UI thread only snapshots GPS values and enqueues — never blocks on DNS/TLS/server round-trips |
| `ota.cpp` | Version check vs `latest.txt` (semver via sscanf), HTTPS firmware download with on-screen progress bar and log; on-boot check waits for WiFi (60 s timeout); when done the underlying page is restored with a clean clear |
| `wifi_manager.cpp` | AP+STA management; add/remove/list saved networks; non-blocking reconnect state machine (rotates credentials with `WiFi.begin()`, polls `WiFi.status()`, no blocking calls in the loop) |
| `http_server.cpp` | On-device WebServer (port 80): provisioning portal + diagnostics endpoints |
| `config_store.cpp` | NVS persistence (`Preferences`, namespace `"wifi"`) |
| `gps.cpp` | UART ingest into TinyGPSPlus (NMEA debug ring buffer removed 2026-08) |
| `serial_buffer.cpp` | Thread-safe 200-line log capture (mutex-guarded — the backend task logs from another thread) so `/serial` can mirror the device log |

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
| `/config` | Provisioning page: WiFi scan dropdown (RSSI, lock icon), password, device name field, manual SSID fallback, timezone select (preselects stored value), speed-unit select, OTA-on-boot checkbox, saved-network list with remove buttons |
| `/save` | Persists SSID/password/timezone/device name/OTA flag, then reboots after 1 s |
| `/wifi/remove` | Removes one saved network |
| `/reset` | Clears ALL WiFi networks and wipes stored config, reboots |
| `/reboot` | Remote restart |
| `/status` | JSON diagnostics: WiFi state, GPS data, heap, version, username |
| `/health` | Plain-text liveness |
| `/serial` | Dumps captured serial log (200 lines) |

No route requires authentication — anyone on the LAN or device AP can wipe or
reboot the device.

A Bruno request collection covering every portal route lives in `bruno/`
(environments: `access-point` = `192.168.4.1`, `local-network` = editable LAN IP).

## OTA updates

- Server: `{OTA_BASE_URL}` = `https://indietrotutta.onrender.com/ota`
- Check: `GET latest.txt`, trimmed to digits/dots, compared as semver
- Download: `HTTPUpdate` of `firmware.bin`, reboot on success, progress bar on TFT
- Triggers: at boot if `config.otaCheckOnStart` is set — armed by `otaInit()`
  and fired from `otaLoop()` only once WiFi is connected (gives up silently
  after 60 s offline)
- Release pipeline: `make dist` bumps `BUILD_VERSION` in `src/config.h`,
  rebuilds, copies `firmware.bin` + writes `latest.txt` into the sibling
  backend repo's `public/ota/`, commits and pushes — devices then self-update

## UI navigation

Four pages in the L-short cycle: MAIN → DIAGNOSTICS → WAYPOINTS → TIMERS → MAIN.
CONFIG is a jump target, not part of the cycle.

Grammar (hints use `L` = left click, `LL` = long left, `R` = right click,
`RR` = long right):

- **Left-short** — leave any submode, else advance to the next page
  (on CONFIG: back to MAIN). Cycles MAIN → WAYPOINTS → TIMERS → CONFIG → MAIN;
  DIAGNOSTICS is **not** in the cycle
- **Right-short** — contextual: advance selection in menus/lists,
  start/stop on TIMERS
- **Left-long** — MAIN: jump to CONFIG · WAYPOINTS: flag position ·
  TIMERS: clear-all · CONFIG: immediate OTA check
- **Right-long** — run the selected entry (menus), delete waypoint (WAYPOINTS),
  lap/reset on TIMERS · MAIN: jump to DIAGNOSTICS

Per screen:

| Screen | Actions |
|---|---|
| MAIN | `L` next page · `LL` CONFIG · `RR` DIAGNOSTICS |
| DIAGNOSTICS | `L` next page (pure stats, two columns) |
| WAYPOINTS | `L` next page · `R` cycle waypoints · `LL` flag here · `RR` delete shown |
| TIMERS | `L` next page · `R` start/stop · `RR` lap while running / reset-all when stopped |
| CONFIG | `L` back to MAIN · `R` select row (OTA-on-boot / speed unit) · `RR` apply · `LL` force OTA now |

Every page switch fills the display black before redrawing (no stale pixels).

## Persistence

NVS (`Preferences`, namespace `"wifi"`):

- Blob key `cfg`: `Config { username[33], timezoneOffsetHours, speedUnit, otaCheckOnStart }`
  (size-checked load; mismatched blobs are deleted and zeroed — a firmware
  update that changes the struct resets timezone/name/speed-unit/OTA flag once;
  WiFi networks are stored under separate keys and survive)
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
- An OTA check that finds no update used to leave its log lines painted over
  the current page (pre-1.0.78); `checkForUpdate()` now waits ~1.5 s and then
  restores the underlying screen via `redrawCurrentPage()` (full clear)
- OTA download blocks the loop until finished
- On the WAYPOINTS screen the bottom status text ("WP n / elapsed") was drawn
  at the same y (235) and font as the button-hint bar, so it painted over the
  hints on every frame once a waypoint existed. `drawBottomBar` now renders it
  above its separator (y=208)
- All web routes unauthenticated; TLS validation disabled everywhere
- Backend "online" tile reflects only the last health/post result
- Backend device registry (`store/deviceStore.js`) and GPS points are
  in-memory — a backend restart loses usernames until the next health poll
  (~30 s) or GPS post re-registers the device

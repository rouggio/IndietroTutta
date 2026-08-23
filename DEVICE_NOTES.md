# IndietroTutta — Device Firmware Notes

Codebase overview for future reference. Generated from source inspection on
2026-08-23, firmware version 1.0.73 — verify against source if the code has
changed since.

## What it is

A portable ESP32 marine GPS instrument: shows speed in knots and course on a
TFT, tracks GPS position to a cloud backend, lets the user flag waypoints,
renders a live map streamed from the server, and self-updates over WiFi.
"Indietro Tutta" is an Italian racing shout ("everyone backwards / reverse all").

## Hardware

| Part | Detail |
|---|---|
| MCU | ESP32 DevKit (`esp32dev` board, Arduino framework) |
| Display | ST7789 240×320 TFT over SPI — CS 5, DC 2, RST 4, MOSI 23, SCLK 18; landscape rotation 3; BGR color order forced by build flag in `platformio.ini` |
| Buttons | GPIO21 = Left, GPIO22 = Right, active-low with pullups; 50 ms debounce, 1000 ms long-press threshold |
| GPS | UART2, RX 16 / TX 17 @ 9600 baud, parsed by TinyGPSPlus |

Display config lives in `include/User_Setup.h`, force-included via
`-DUSER_SETUP_LOADED -include include/User_Setup.h` in `platformio.ini`.

## Module map (`src/`)

Single cooperative superloop in `main.cpp` — no RTOS tasks anywhere.
Loop order: serial buffer → buttons → GPS parse → screen draw → WiFi/DNS/web
server → backend.

| Module | Role |
|---|---|
| `screens.cpp` | Owns global `TFT_eSPI`; 3-page UI router; 200 ms redraw throttle; page cycling skips the map page |
| `screen_one.cpp` | Main instrument: big speed, course/cardinal, status tiles (WiFi/cloud/GPS fix), satellite count, time/date bar with timezone offset; waypoint sub-modes |
| `screen_map.cpp` | Fetches raw RGB565 framebuffer from backend and pushes it to the TFT in 320×10 chunks; 5 s stall timeout; full-screen error states |
| `screen_two.cpp` | Diagnostics page (GPS stats, IP, version) + menu: check OTA now, OTA-on-boot toggle, reboot |
| `splash_screen.cpp` | Static title, blocking `delay(2000)` |
| `buttons.cpp` | Polling driver emitting Press/LongPress/Release events to one callback |
| `backend.cpp` | Health GET every 30 s; position POST every 40 s when fix is valid; immediate flagged-position POST on demand |
| `ota.cpp` | Version check vs `latest.txt` (semver via sscanf), HTTPS firmware download with on-screen progress bar and log |
| `wifi_manager.cpp` | AP+STA management over WiFiMulti, add/remove/list saved networks, reconnect loop |
| `http_server.cpp` | On-device WebServer (port 80): provisioning portal + diagnostics endpoints |
| `config_store.cpp` | NVS persistence (`Preferences`, namespace `"wifi"`) |
| `gps.cpp` / `gps_debug.cpp` | UART ingest into TinyGPSPlus + 30-line NMEA ring buffer |
| `serial_buffer.cpp` | 200-line log capture so `/serial` can mirror the device log |

## Backend communication

Base URL compiled in (`src/config.h`): `https://indietrotutta.onrender.com`.

| Call | Direction | When | Notes |
|---|---|---|---|
| `GET {BASE}/health` | out | every 30 s | Headers `DeviceId: <WiFi MAC>` + `Username: <name>` ⇒ sets online flag and re-registers the device |
| `POST {BASE}/gps` | out | every 40 s with valid fix | JSON: lat/lon (7 dp), speed (kn), course, altitude, sats, flagged, username |
| Flagged post | out | Right-long on main screen | Same payload with `"flagged": true` |
| `GET {BASE}/map/device.rgb565?width=320&height=240` | in | entering map page | Raw framebuffer rendered directly |

No authentication: identity is the `DeviceId` MAC header; the username is a
display label attached to that identity on the backend (whitelist-sanitized on
both sides). All TLS uses `WiFiClientSecure::setInsecure()` everywhere
(backend, OTA, map).

## Device web portal (port 80)

Device always boots as open softAP **"IndietroTutta"** plus STA; DNS server
points all names at `192.168.4.1`. Joining the AP and opening any URL redirects
to `/config`. Routes (`src/http_server.cpp`):

| Route | Purpose |
|---|---|
| `/config` | Provisioning page: WiFi scan dropdown (RSSI, lock icon), password, device name field (shown on the map), manual SSID fallback, timezone select, saved-network list with remove buttons |
| `/save` | Persists SSID/password/timezone/OTA flag, then reboots after 1 s |
| `/wifi/remove` | Removes one saved network |
| `/reset` | Clears ALL WiFi networks and wipes stored config, reboots |
| `/reboot` | Remote restart |
| `/status` | JSON diagnostics: WiFi state, GPS data, last 30 NMEA lines, heap, version |
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

Global: redraw every 200 ms; button events routed to the active page.

- **Left-short**: cycle pages (main ↔ diagnostics; map skipped)
- **Right-short**: jump to map page (from main)
- **Right-long on main**: flag current GPS position locally (max 10 markers,
  FIFO eviction) and post it to the backend
- **Left-long**: opens per-page menu (waypoint list on main; OTA/reboot menu on
  diagnostics)
- In waypoint list: view lat/lon + elapsed timer per marker, delete current

## Persistence

NVS (`Preferences`, namespace `"wifi"`):

- Blob key `cfg`: `Config { username[33], timezoneOffsetHours, otaCheckOnStart }`
  (size-checked load; mismatched blobs are deleted and zeroed — a firmware
  update that changes the struct resets timezone/OTA flag once; WiFi networks
  are stored under separate keys and survive)
- Per-network keys `wifi_%d_ssid` / `wifi_%d_pass` plus `wifi_count`, max 10

Compiled in (`src/config.h`): `BASE_URL`, `OTA_BASE_URL`, `BUILD_VERSION`.
Pins are hardcoded in `include/User_Setup.h` and module sources.

## Known quirks / gotchas

- Saving via the web portal always resets `otaCheckOnStart=false` because the
  form omits the field while the handler treats absence as false
  (`http_server.cpp`)
- Blank device name in the portal keeps the stored username (only non-empty
  sanitized values overwrite it)
- Usernames are restricted to `[A-Za-z0-9 ._-]`, max 32 chars, on both device
  and backend, so they are safe to render unescaped in the browser
- WiFi reconnect uses `wifiMulti.run(5000)` — each retry can block the whole
  loop up to 5 s (UI freezes during reconnects)
- The stored `endpoint` NVS field is dead weight — URLs are compile-time only
- Timezone offset is re-read from NVS on every 200 ms UI frame
- Splash blocks for 2 s; OTA and map streaming also block the loop
- All web routes unauthenticated; TLS validation disabled everywhere
- Backend "online" tile reflects only the last health/post result
- Backend device registry (`store/deviceStore.js`) and GPS points are
  in-memory — a backend restart loses usernames until the next health poll
  (~30 s) or GPS post re-registers the device

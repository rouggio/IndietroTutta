# AGENTS.md — IndietroTutta device firmware (ESP32)

Portable marine GPS instrument: ST7789 240×320, 2 buttons, GPS UART2, WiFi AP+STA,
OTA self-update. Main screen body = 2 rows × 3 cols: instant speed (row 1,
cols 1-2), max speed + session time (row 2), wind/bearing ring cell (col 3,
spans both rows). Top bar (icons) + bottom hints untouched.

## Hardware / toolchain

- Board `esp32dev`, Arduino, libs: `TFT_eSPI + TinyGPSPlus + ArduinoJson@^6.21.1`.
- Display ST7789 CS5 DC2 RST4 MOSI23 SCLK18, rotation 3, `TFT_BGR` in `platformio.ini`.
  Pins in `include/User_Setup.h` force-included via `-DUSER_SETUP_LOADED -include`.
- Buttons GPIO21=Left GPIO22=Right, active-low, 50ms debounce, 500ms long-press.
- GPS UART2 RX16 TX17 @9600, parsed by TinyGPSPlus in `gps.cpp`.
- Build: `make compile` (must be SUCCESS before commit), `make upload PORT=COM5`,
  `make monitor`, `make dist` (bump + OTA publish), `make dl` (local deploy:
  ota-local + device pulls immediately, `DEVICE_IP ?= 192.168.0.106`). Toolchain: `.venv` PlatformIO.

## Architecture — superloop + backend task

`src/main.cpp loop()`: serial_buffer → buttons → gps → screens → wifi → backend → ota.
Two FreeRTOS tasks in `backend.cpp` (12288 stack each, core 0): feed task
(mock poll + telemetry upload + deletes + template fetch; slow passes
>2s logged with leg split) and health task (heartbeat 30s idle / 5s live
+ run upload) so a slow `/health` never starves the GPS feed.

- UI thread only snapshots GPS + `enqueueWork()` (16-deep queue, drops oldest).
  Waypoints carry a device-generated `uid` (`wp-<millis>-<seq>`, stored on the
  marker); deletes go through an 8-deep uid queue (`DELETE /gps/flagged`,
  retried until sent/gone).
- Task drains queue → `POST /gps`, plus `GET /health` every 30s.
- `backendLoop()` adaptive GPS throttle: `30s@0kn → 2s@5kn` linear (`gpsIntervalForSpeed`).
- `backend.h: backendOnline()`, `backendSendFlaggedPosition()`, `backendInit/Loop`.
  Telemetry uploads carry `simulated:true` when mock GPS is active (map shows
  sim tracks); flagged waypoints + deletes stay real-only (suppressed in mock).
- `serial_buffer.cpp`: 200-line mutex-guarded log for `/serial`.

Key modules: `screens.*` router + 200ms throttle, `screen_speed.*` main
(body grid 2×3 under the top bar: instant speed cell = row1 cols1-2 —
"SPEED (kn)" label + font-8 value, font 7 when 5 chars ("123.4"); row2 col1
Max speed, col2 Session; col3 ring cell spans both rows — N-up ring
top-aligned (diameter = cell width − 8px), "N" inside the top, two solid
EQUILATERAL triangles stacked radially without overlap: green boat (HALF
the shared size, tip tangent to the ring border, base pushed out along the
bearing) + red wind (same size, tip tangent to the boat base, tip downwind
= windDir+180; session wind wins, else env wind); below the ring WND/BRG
gray font-2 labels + GLCD font-1 values (drawLabel1, fixed 3 chars; bearing
with a drawn degree ring; wind = knots, no unit string); values redrawn only on change, arrows
repaint only when an angle changed (prev -1/-2 sentinels), one-off fillRect
wipe only on speed-width change; POS line removed),
`screen_race.*` shared RACE/PRAC screen (map viewport = left 4/5; wireframe +
boat triangle/edge-dot + ~1 cm dashed yellow stub toward the next mark; right pane =
6 equal rows from the very top, labels via `drawLabel1`, values font-2 right-aligned,
WND row = wind arrow + whole-knot speed; mark numbers drawn above the circles;
N-UP/BRG-UP/FIT view tag; empty state clears map+pane and shows a 3-line message;
map ≤1Hz, text padded),
`gps_mock.*` scripted GPS for indoor testing (synthetic RMC+GGA from `GET /sim/next`
into `gps.encode()`; NVS flag; uploads suppressed; MOCK banner; portal `/mock`),
`race_session.*` health-pulled session cache (marks/lines/windDir+windSpeed/startTime+offset,
NVS `race` ns, ArduinoJson heap doc; explicit unassigned clears it, offline keeps cache;
health top-level `wind` → envWindDir/envWindSpeed RAM-only fallback: UI compass +
practice Start/Repeat placement use it when the session carries no wind),
`screen_waypoints.*` (LL flag, max 10 FIFO RAM-only),
`screen_diagnostics.*` (RR from main), `screen_config.*` (LL from main),
`wifi_manager.*` non-blocking AP+STA (scan prefers visible strongest, park/retry),
`ota.*` semver vs `latest.txt`, `http_server.*` portal port 80,
`canvas.*` drawing-target indirection (`gCanvas` = hw panel; `/screen`
grab re-renders the current page into an off-screen 8bpp sprite since the
ST7789 is write-only — `#define tft (*gCanvas)`; font 1 on a sprite faults,
so capture uses font 2 for the race pane labels),
`config_store.*` NVS ns `wifi` blob `cfg` + `wifi_%d_ssid/pass`.
(Race engine now lives in `race_session.*` + `race_run.*` + `race_templates.*` + `screen_race.*`.)

## UI navigation (hints: L/LL left, R/RR right)

- L-cycle = MAIN → WAYPOINTS → RACE → MAIN (`PAGE_CYCLE=3`). DIAGNOSTICS + CONFIG excluded.
- MAIN: `L` next, `LL`→CONFIG, `RR`→DIAGNOSTICS. RACE: `L` next, `LL` menu (practice: Start→template browse / Repeat→re-anchor+gun / Abandon; race: Resync/Abandon; `R` cycles, `RR` picks, `L` backs out), browse: `R` next template, `RR` pick (+10s gun, line 20m upwind), `L` back. `RR` cycles N-UP → BRG → FIT. DIAGNOSTICS/CONFIG: `L` back to MAIN.
- WAYPOINTS: `R` cycle, `LL` flag (also `POST /gps flagged:true`), `RR` delete.
- CONFIG: `R` select row, `RR` apply, `LL` force OTA now.
- Every page switch full-black clear; ghost-clear readouts in speed.
- NO fillRect/fillScreen/clear on the 200ms refresh path — it flickers. Overwrite
  text in place with space padding instead, e.g. `" " + val + " "` (both sides
  for MC_DATUM, leading space suffices for TR_DATUM). Static labels can just be
  redrawn with the same string. Full clear only on page switch / init / mode toggle.
- `/screen` re-renders cleanly, so it HIDES incremental-redraw ghosting (stale pixels,
  missing grid/labels) — verify that class of bug on the PANEL, not the grab. Font 1
  (GLCD) drawn into a `TFT_eSprite` faults intermittently → small labels use
  `drawLabel1()`/`drawLabel1C()` in `canvas.h` (manual GLCD via `drawPixel`).
- Race screen: full-screen overlays (menu, template browse) are removed via
  `redrawCurrentPage()`; a course change that closes them forces it too, and
  `resetRaceText()` runs on `courseChanged` so the pane (grid+labels) always repaints.
  `gGrabbing` (set during a `/screen` capture) skips side effects (race-run reset,
  menu auto-close).
- `src/config.h`: `BASE_URL`, `OTA_BASE_URL` (both prod Render), `BUILD_VERSION` (local-dev 1.0.166).
- Device reports fw: `Firmware-Version: BUILD_VERSION` header on `GET /health` +
  `POST /gps`, plus `"fw"` in gps JSON body.

## Network / portal / OTA

- `GET /health` headers `DeviceId:<MAC>` + `Username:`; response body parsed for
  `session` push (course + startTime + offset, cached in NVS) and top-level `wind`
  (venue wind at the boat's last stored position, env fallback, RAM); `POST /gps`
  JSON lat/lon/speed/course/alt/sats/flagged/username. `setInsecure()` everywhere, no auth.
- `server_link.*`: dev/prod switch (NVS `srv` mode/host, portal `/server`); dev+host = plain HTTP to LAN backend, else TLS prod. All fetchers (`health/gps/sim/templates/OTA`) go through `ServerLink`.
- Portal always up: open AP `IndietroTutta`, DNS → `192.168.4.1` → `/config`.
  Routes: `/config /save /wifi/remove /reset (wipe all!) /reboot /status /health /serial /mock?on=1|0 /ota (POST immediate check vs current server) /server?mode=prod|dev&host=<ip:port> (GET=current) /btn?b=L|R&e=R|RR (remote button) /screen (RGB565 BE 320x240 grab: re-renders the current page into an off-screen 8bpp sprite and streams it, since the ST7789 can't be read back; `scripts/grab_screen.py` → PNG)`. All unauthenticated.
- OTA: `GET ota/latest.txt` → semver compare → `HTTPUpdate firmware.bin` + progress bar + `redrawCurrentPage()`. `rebootOnUpdate(false)`: on success the panel gets a clean `fillScreen` before `ESP.restart()` so the boot splash shows neatly (no stale progress-overlay pixels). Boot check if `otaCheckOnStart`, 60s WiFi timeout. `make ota-local` stages a dev build into LAN `public/ota/` with no commit/push/hook (cloud untouched; `config.h` + `public/ota/*` stay dirty by design); `make dl` = ota-local + immediate pull + verify.
- Bruno in `bruno/` covers portal routes (`access-point` + `local-network` envs).
- Quirks: empty portal name keeps stored username; username regex both sides; WiFi rotate-on-5s-fail never blocks UI; OTA download blocks loop; waypoints/laps RAM-only.

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
  `make monitor`, `make dist` (= shorthand `dc`: bump + publish OTA), `make dl`
  (= shorthand `dl`: ota-local + device pulls immediately, `DEVICE_IP ?= 192.168.0.106`
  but the IP is DHCP — pass `DEVICE_IP=...`). Toolchain: `.venv` PlatformIO.
  Shorthands `sl`/`sc`/`dl`/`dc` and the working modes `devl`/`devc` are defined in the
  root `AGENTS.md`.

## Architecture — superloop + backend task

`src/main.cpp loop()`: serial_buffer → buttons → gps → screens → wifi → backend → ota.
Two FreeRTOS tasks in `backend.cpp` (12288 stack each, core 0): feed task
(mock poll + telemetry upload + deletes + course fetch; slow passes
>2s logged with leg split) and health task (heartbeat 30s idle / 5s live
+ run upload) so a slow `/health` never starves the GPS feed.

- UI thread only snapshots GPS + `enqueueWork()` (16-deep queue, drops oldest).
- Task drains queue → `POST /gps` (always sends `flagged:false`; the device no longer
  creates waypoints — flagged/deleted points are a WEB-side feature, backend
  `/gps?flagged=true` + `DELETE /gps/flagged` stay for the builder's waypoint adopter),
- Task drains queue → `POST /gps`, plus `GET /health` every 30s.
- `backendLoop()` adaptive GPS throttle: `30s@0kn → 2s@5kn` linear (`gpsIntervalForSpeed`).
- `backend.h: backendOnline()`, `backendOnlineFresh()` (health <60s old — gates
  the race screen), `backendFetchCourses()`, `backendCreateSession()` +
  `backendSessionCreated()` (async POST /sessions, id adopted by the screen),
  `backendAbandonSession()` (POST signals ABANDON), `backendSendFlaggedPosition()`, `backendInit/Loop`.
  Telemetry uploads carry `simulated:true` when mock GPS is active (map shows
  sim tracks),
- `serial_buffer.cpp`: 200-line mutex-guarded log for `/serial`.

Key modules: `screens.*` router + 200ms throttle, `screen_speed.*` main
(body grid 2×3 under the top bar: instant speed cell = row1 cols1-2 —
"Speed (kn)" label at y=40 + font-8 value centered y=105, font 7 when 5chars
("123.4"), one-off fillRect wipe y59..148 on width change; `ROW_MID=152` gives
the speed row 121px; row2 col1 "Max Speed", col2 "Session time" — labels
font-2 gray at ROW_MID+7, values font-4 at ROW_MID+42, each center-wiped when
narrower; col3 ring cell spans both rows — N-up ring
top-aligned (diameter = cell width − 8px), RING_GRAY (0xC618) ring + 30° radial
ticks inside (twice
as long at 90/180/270, top skipped for the font-2 "N" which moves to the
true-north angle in bearing-up view), two solid equilateral-shape minus-5px
triangles stacked radially without overlap (green boat: tip tangent to the
ring border, base pushed out along the bearing; red wind: same size, tip
tangent to the boat base, tip downwind = windDir+180; each with a 4px-thick
15px color-matched tail; session wind wins, else env wind), plus the no-go
arc (9px radial band, pure TFT_RED, width via /nogo, centered on the upwind
direction opposite the red tip, moves bearing-relative);
below the ring "Wind (kn)" (x=221) and "BRG" (x=290) gray font-2 labels at
y=166 + font-4 values at y=187 (wind centered at x-8, bearing right-anchored
at x+20 with a drawn degree ring at x+26) — all four redraw only on change
(prev -1/-2 sentinels), one-off fillRect
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
`screen_diagnostics.*` (RR from main), `screen_config.*` (LL from main),
`wifi_manager.*` non-blocking AP+STA (scan prefers visible strongest, park/retry),
`ota.*` semver vs `latest.txt`, `http_server.*` portal port 80,
`canvas.*` drawing-target indirection (`gCanvas` = hw panel; `/screen`
grab re-renders the current page into an off-screen 8bpp sprite since the
ST7789 is write-only — `#define tft (*gCanvas)`; font 1 on a sprite faults,
so capture uses font 2 for the race pane labels),
`config_store.*` NVS ns `wifi` blob `cfg` + `wifi_%d_ssid/pass`.
(Race engine now lives in `race_session.*` + `race_run.*` + `race_courses.*` + `screen_race.*`.)

## UI navigation (hints: L/LL left, R/RR right)

- L-cycle = MAIN → RACE → MAIN (`PAGE_CYCLE=2`; the WAYPOINTS page was REMOVED on
  `feature/remove-waypoints` `9e55584`, flagged-post/delete plumbing included).
  DIAGNOSTICS + CONFIG excluded.
- MAIN: `L` next, `LL`→CONFIG, `R` toggles the ring view N-up ↔ bearing-up
  (RAM; boat triangle to the top, wind bearing-relative, N at true north),
  `RR`→DIAGNOSTICS. RACE: `L` next, `RR` cycles N-UP → BRG → FIT,
  `LL`→MENU. DIAGNOSTICS/CONFIG: `L` back to MAIN.
- RACE MENU `RACE/PRACTICE MENU` — rows are BUILT from live state
  (`buildMenuRows`), so you can never be offered an action that makes no
  sense: **Start** only with no session, **Repeat** only when a course is
  remembered and the session is not a committee race, **Resync** only in a
  race, **Abandon** only with a session. `R` cycles, `RR` picks, `L` backs out.
- PRACTICE SETUP: Start → course picker (`R` next, `RR` choose, `L` back;
  pre-selected on the remembered course, else Windward-Leeward) → **Options**
  (`R` cycles the highlighted row's value, `L` moves between the two rows,
  `LL` back to the list, `RR` start). Gun 10/30/60/120/300s, line 10/20/30m;
  the line sits `distM` metres UPWIND of the boat. Values + course are
  remembered in NVS (`race` ns `prefCourse`/`prefGun`/`prefDist`) and are
  written **only on confirm**, so browsing never changes a default.
- Confirming a practice start does BOTH: builds the local session (countdown
  to `startTime = now + gun`) and `POST /sessions` on the backend task with
  `courseId` + placement + `startTime` + this boat, so the session is real on
  the web; the returned id is adopted when it lands. A failed POST keeps the
  local session (transient `OFFLINE`). **Repeat** raises ABANDON on the
  current session then re-creates the remembered one re-anchored at the boat
  (it re-fetches the library first when the RAM pool was wiped by a reboot).
- The race screen only OPENS when `/health` succeeded in the last 60s
  (`backendOnlineFresh()`, `sys.onlineFresh` in `/status`); otherwise `L` shows
  a `NO NETWORK` note and stays put. Losing the link MID-session never ejects.
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
- Race screen: full-screen overlays (menu, course browse/options) are removed via
  `redrawCurrentPage()`; a REAL session change closes them too, and
  `resetRaceText()` runs on `courseChanged` so the pane (grid+labels) always repaints.
  `gGrabbing` (set during a `/screen` capture) skips side effects (race-run reset,
  menu auto-close).
- **`courseChanged` must mean "the session changed", never "a repaint was
  forced".** `lastRaceCourseKey` used to be reset to a `-2` sentinel inside
  `if (requiresInit)` to force a map redraw, so the very next pass read as a
  course change and closed the menu/picker on the pass it was opened — the
  course picker never appeared at all. Map-redraw (`mapDirty`) and
  session-change (`courseChanged`) are separate flags now.
- Anything an overlay needs must be sampled OUTSIDE the branches that overlays
  short-circuit: `drawRaceText` is skipped while a menu/picker is open, so the
  live-fix snapshot moved to `raceSampleFix()` at the top of `drawScreenRace`
  (otherwise "is my fix fresh?" failed after 15s of looking at the menu).
- **`src/config.h`: `BASE_URL`, `OTA_BASE_URL` (both prod Render), `BUILD_VERSION` (local-dev 1.0.189).**
- Device reports fw: `Firmware-Version: BUILD_VERSION` header on `GET /health` +
  `POST /gps`, plus `"fw"` in gps JSON body.

## Network / portal / OTA

- `GET /health` headers `DeviceId:<MAC>` + `Username:`; response body parsed for
  `session` push (course + startTime + offset, cached in NVS) and top-level `wind`
  (venue wind at the boat's last stored position, env fallback, RAM); `POST /gps`
  JSON lat/lon/speed/course/alt/sats/flagged/username. `setInsecure()` everywhere, no auth.
- `server_link.*`: dev/prod switch (NVS `srv` mode/host, portal `/server`); dev+host = plain HTTP to LAN backend, else TLS prod. All fetchers (`health/gps/sim/courses/OTA`) go through `ServerLink`.
- **`/screen` capture needs ~77 KB of CONTIGUOUS heap and starves on prod.** A 320×240
  8bpp sprite is one 76,801-byte `calloc`; when it fails the route answers
  `503 no memory for capture`. Right after boot `maxAllocHeap` is ~77,812 and it works,
  but TLS churn from talking to **Render** fragments the heap down to ~40,948 and every
  grab then fails until a reboot. Symptom to recognise: `GET /status` fine, `/screen` 503,
  and the serial full of `[BACKEND] slow pass 3000ms+` (each mock fix + `POST /gps` to a
  cold free instance takes seconds). On the local DEV backend it works all session. Reboot
  before a UI-debugging stretch, or point the device at DEV; a real fix would be streaming
  the page in 320×40 bands (12.8 KB per sprite) instead of one big sprite.
- Portal always up: open AP `IndietroTutta`, DNS → `192.168.4.1` → `/config`.
  Routes: `/config /save /wifi/remove /reset (wipe all!) /reboot /status /health /serial /mock?on=1|0 /nogo (GET current; POST /nogo?deg=<total width 10..180> — no-go arc width, own NVS key, shown in /status) /ota (POST immediate check vs current server) /server?mode=prod|dev&host=<ip:port> (GET=current) /btn?b=L|R&e=R|RR (remote button) /screen (RGB565 BE 320x240 grab: re-renders the current page into an off-screen 8bpp sprite and streams it, since the ST7789 can't be read back; `scripts/grab_screen.py` → PNG)`. All unauthenticated.
- OTA: `GET ota/latest.txt` → semver compare → `HTTPUpdate firmware.bin` + progress bar + `redrawCurrentPage()`. `rebootOnUpdate(false)`: on success the panel gets a clean `fillScreen` before `ESP.restart()` so the boot splash shows neatly (no stale progress-overlay pixels). Boot check if `otaCheckOnStart`, 60s WiFi timeout. `make ota-local` stages a dev build into LAN `public/ota/` with no commit/push/hook (cloud untouched; `config.h` + `public/ota/*` stay dirty by design); `make dl` (= `dl`) = ota-local + immediate pull + verify, and `make dist` (= `dc`) is the cloud publish that commits the bin into the backend repo.
- Bruno in `bruno/` covers portal routes (`access-point` + `local-network` envs).
- Quirks: empty portal name keeps stored username; username regex both sides; WiFi rotate-on-5s-fail never blocks UI; OTA download blocks loop; laps RAM-only.
- **Never edit a UTF-8 source with PowerShell** (`Get-Content -Raw` + `Set-Content
  -Encoding UTF8`): it reads with the ANSI code page and writes back double
  encoded, plus a BOM. That silently mangled `src/screen_race.cpp` (every `—`,
  `°`, `→` in a comment). Use the edit tool, or Node. If it happens, `node
  tools/fix_cp1252.js <file>` reverses the cp1252 double encode and refuses to
  write anything still dirty.
- **The device's IP is DHCP and it roams between Wi-Fi networks.** Before any
  remote test, `GET /status` for the current IP/SSID; if it joined another
  subnet it cannot reach the LAN DEV backend, and `POST /ota` then fails
  *silently* (the pull just never lands) — check `sys.server` in `/status` and
  re-point it with `POST /server?mode=dev&host=<laptop-ip>:3000` (that route is
  **POST-only** for writes; GET only reports).

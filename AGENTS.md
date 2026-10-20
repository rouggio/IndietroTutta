# AGENTS.md — IndietroTutta device firmware (ESP32)

Portable marine GPS instrument: ST7789 240×320, 2 buttons, GPS UART2, WiFi AP+STA,
OTA self-update. Main screen body = 2 rows × 3 cols: instant speed (row 1,
cols 1-2), max speed + session time (row 2), wind/bearing ring cell (col 3,
spans both rows). Top bar (WiFi/data/FX icons + DEV tile when server mode is DEV) +
bottom hints untouched.

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
triangles sharing the ring tangent (green boat: tip tangent to the
ring border, base pushed inward along the bearing; red wind: same size and
same radial span, tip downwind = windDir+180, so the arrows can overlap at
close angles; each with a 4px-thick
15px color-matched tail; session wind wins, else env wind), center boat/wind
angle (font 2 + drawn degree ring: 0 dead-on upwind, 180 dead downwind),
plus the no-go
arc (9px radial band, pure TFT_RED, width via /nogo, centered on the upwind
direction opposite the red tip, moves bearing-relative);
below the ring "Wind (kn)" (x=221) and "BRG" (x=290) gray font-2 labels at
y=166 + font-4 values at y=187 (wind centered at x-8, bearing right-anchored
at x+20 with a drawn degree ring at x+26) — all four redraw only on change
(prev -1/-2 sentinels), one-off fillRect
wipe only on speed-width change; POS line removed),
`screen_race.*` shared RACE/PRAC screen (map viewport = left 4/5; wireframe +
boat triangle/edge-dot + ~1 cm dashed yellow stub toward the next mark (2/3 the
length of the COG stub) + a cyan dashed stub along the boat's own COG (`gps.course`,
only above 0.5 kn, clipped to the map rect — it can point into the header band);
right pane =
6 equal rows from the very top, labels via `drawLabel1`, values font-2 right-aligned,
WND row = wind arrow + whole-knot speed; mark numbers drawn above the circles;
N-UP/BRG-UP/FIT view tag; empty state clears map+pane and shows a 3-line message;
map ≤1Hz, text padded),
`gps_mock.*` scripted GPS for indoor testing: ONE request buys the WHOLE
journey (`GET /sim/tape`, CSV, ~18KB) into a 640-fix tape in RAM, released one
fix per second and synthesised into RMC+GGA for `gps.encode()` (the endless
wander is refilled 30 fixes at a time through the same URL, same player). So
the network can delay the START of a mock race but can never punch a hole in
one — the old 30-fix, wall-clock-sliced window both skipped race 182's first
130s and dropped 13-fix lumps whenever the device ran ahead of it. **A scripted
journey also waits for `page == RACE` and a valid session** before its first
fix: it is only scored there, so sailing it while parked on MAIN wasted its
front (which is the other half of how 182 broke). NVS flag; uploads suppressed;
MOCK banner; portal `/mock`),
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
**The race clock base is the GUN, never the line crossing:** every start path in
`race_run.cpp` sets `startEpoch = gun`, so the panel clock, `splits[]` and the
uploaded `elapsedSec` are all gun-relative (a late or OCS-returning start costs
what it lost). The START **event** still lands on the crossing — that is what the
spec scores.
**Mark-passing law: `SPEC-mark-passing.md` at the repo root — LOCKED, read it
before touching any pass/score/target logic.**

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
- The race screen **always opens** — there is no link gate (todo 42 removed it).
  The session is cached in NVS (kept while offline), the run upload retries until
  it lands, and a practice start whose `POST /sessions` fails keeps a local
  `OFFLINE` session on purpose. With nothing to show, the page's own empty state
  says so. `sys.onlineFresh` in `/status` is a **diagnostic only** now. Losing the
  link MID-session never ejects either.
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
- Race screen states: **empty / pre-start / racing / finished**. On the finish
  edge the screen snapshots the result (todo 38): `FINISHED` in green in the
  banner slot and the elapsed time frozen in the header, both held **after the
  backend drops the finished session** (it returns `session:null` seconds after
  the run uploads) until a new run/session arrives or the page is left
  (`raceFinishHoldClear()` from `screens.cpp`). The distance readout blanks while
  held. Banner precedence: signal (8 s) → FINISHED → countdown → OCS → transient.
- Race screen: full-screen overlays (menu, course browse/options) are removed via
  `redrawCurrentPage()`; a REAL session change closes them too, and
  `resetRaceText()` runs on `courseChanged` so the pane (grid+labels) always repaints.
  `gGrabbing` (set during a `/screen` capture) skips side effects (race-run reset,
  menu auto-close).
- Map labels: coincident marks share one label ("1-3") and it is drawn **per
  index** (todo 40) — the target's own index in `RBOAT`, every other index
  `RDIM`, `-` separators dim. The whole string is measured with `textWidth()`
  and centred once, so the label's position never shifts as the target moves;
  `ML_DATUM` is used for the runs and `MC_DATUM` restored after. A gate target
  highlights **both** indices, and a lone mark's label highlights too — there is
  no separate "pile" case.
- Mark drawing, in z-order: **circle = the rounding radius** (`m.r`, 30 m by
  default — NOT the buoy), **four saw-teeth on the arc = the rounding
  direction** (todo 44), **red dot = the buoy itself** (todo 43), then the
  deferred target ring on top. The teeth sit at 45/135/225/315, phase fixed to
  the screen (they do not rotate with bearing-up/fit), clockwise for `S`, and
  each is a saw-tooth whose **long side is the arc itself** (filled right up to
  the ring, `2L / rPx` of arc, capped at 75°) with its **short side standing out
  of the circle at the trailing end** — long = 2 × short,
  `2*rPx/5` floored at 4 px and capped at 10 px. The tangent-based variant
  floated off the ring, so do not go back to a straight long side. They take the
  circle's colour (white, `RBOAT` on the target); gates get none. The red dot is
  `buoyDiam / 2 / DEG_M * p.scale` with a 2 px floor and a 6 px cap, **one per
  physical buoy** (a pile shares the label's coincident-position test).
  Red is therefore the page's **buoy + alert** colour, not alert-only.
- The **map scale** (todo 45) is map chrome, drawn LAST in `drawStaticLayer` so
  a course line never cuts it: the map's **bottom-right corner is the zero**
  (`zeroX = MAP_W - 4`, 2 px of air before the frame), the bar runs left from
  it with a tick at each end, and the label sits above, right-aligned. Its
  length must stay between **1/5 and 1/2 of the map frame's height** (30..75 px
  at the current 150 px map) and the DISTANCE is what gets chosen, from a 1-2-5
  sequence, largest that fits — so the label is always a round number and never
  lies about the bar (a fixed 10 px bar would be off by 2x at the focus zoom).
  `RDIM` like the rest of the chrome, and the label **must use font 2** (font 1
  digits are broken on the sprite). Panel-verified: 100 m/73 px overview,
  20 m/46 px next-target.
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
  the page in 320×40 bands (12.8 KB per sprite) instead of one big sprite. The grab is an
  **8bpp RGB332** sprite: colours are quantised (e.g. `RDIM` 0x8410 comes back as a nearby
  palette value), so a pixel probe must match by tolerance, never by exact RGB565.
- Portal always up: open AP `IndietroTutta`, DNS → `192.168.4.1` → `/config`.
  Routes: `/config /save /wifi/remove /reset (wipe all!) /reboot /status /health /serial /mock?on=1|0 /nogo (GET current; POST /nogo?deg=<total width 10..180> — no-go arc width, own NVS key, shown in /status) /ota (POST immediate check vs current server) /server?mode=prod|dev&host=<ip:port> (GET=current) /btn?b=L|R&e=R|RR (remote button) /screen (RGB565 BE 320x240 grab: re-renders the current page into an off-screen 8bpp sprite and streams it, since the ST7789 can't be read back; `scripts/grab_screen.py` → PNG)`. All unauthenticated.
- **`sys.page` in `/status`** (`main|race|diagnostics|config`): the mock feed
  delivers the scripted run whatever page is showing, so a device parked on
  MAIN produces a race with no countdown, no marks and no finish — and
  nothing in `/status` used to distinguish that from a device that is
  racing. **Always verify `sys.page` before a scripted test**; `L` cycles
  RACE → MAIN, so a second blind `/btn` press silently parks the boat.
  Since todo 30 the page comes up by itself when a session is newly assigned
  (`backendTakeRacePageRequest()` → `setCurrentPage(PageRace)` from MAIN
  only, once, so a skipper who leaves is not dragged back), and a scripted
  journey will not release a fix until the race screen is up — the mock boat
  simply waits instead of sailing its front unwatched.
- **Diagnostic logging is rationed on purpose.** The 200-line serial ring
  rolls in ~3 minutes and a race does not fit in it. The start-window trace
  logs only on a CHANGE of side/near (~4 lines per race); an earlier
  once-a-second version pushed `passed mark` and `finished` out of the ring
  — the diagnostic destroyed the evidence it was added to collect.
- OTA: `GET ota/latest.txt` → semver compare → `HTTPUpdate firmware.bin` + progress bar + `redrawCurrentPage()`. `rebootOnUpdate(false)`: on success the panel gets a clean `fillScreen` before `ESP.restart()` so the boot splash shows neatly (no stale progress-overlay pixels). Boot check if `otaCheckOnStart`, 60s WiFi timeout. `make ota-local` stages a dev build into LAN `public/ota/` with no commit/push/hook (cloud untouched; `config.h` + `public/ota/*` stay dirty by design); `make dl` (= `dl`) = ota-local + immediate pull + verify, and `make dist` (= `dc`) is the cloud publish that commits the bin into the backend repo.
- Bruno in `bruno/` covers portal routes (`access-point` + `local-network` envs).
- Quirks: empty portal name keeps stored username; username regex both sides; WiFi rotate-on-5s-fail never blocks UI; OTA download blocks loop; laps RAM-only.
- **Never edit a UTF-8 source with PowerShell** (`Get-Content -Raw` + `Set-Content
  -Encoding UTF8`): it reads with the ANSI code page and writes back double
  encoded, plus a BOM. That silently mangled `src/screen_race.cpp` (every `—`,
  `°`, `→` in a comment). Use the edit tool, or Node. If it happens, `node
  tools/fix_cp1252.js <file>` reverses the cp1252 double encode and refuses to
  write anything still dirty.
- **The device's IP is DHCP and it roams between Wi-Fi networks** — moving between APs
  around the house changes it without warning. Known leases (same ones recur
  per AP): `192.168.0.106` on SSID `Studio`, `192.168.1.13` on SSID
  `Vodafone-Rouggio`. Before any remote test, **do not guess the
  address by trying candidates: if the device landed on another subnet its portal is
  unreachable and you have nothing to query.** Ask the dev backend, which records the LAN
  IP of every heartbeat (`curl localhost:3000/devices` → `ip`); the device is already
  heartbeating there in `devl` mode. Keep
  `bruno/environments/access-point.bru` in step with whatever `ip` comes back, and if the
  **laptop** moved too, re-point the device with
  `POST /server?mode=dev&host=<laptop-ip>:3000` (that route is **POST-only** for writes).
  Same-subnet moves can land on the same leases, so re-checking is cheap and often a
  no-op — but the failure is silent when it isn't.

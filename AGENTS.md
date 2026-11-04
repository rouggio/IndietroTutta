# AGENTS.md — IndietroTutta device firmware (ESP32 v1 / ESP32-S3 v2)

Portable marine GPS instrument: ST7796 **480×320** on the ESP32-S3 (the only board
since 2026-11-01; the classic-ESP32 240×320 build is retired), 3 buttons, GPS
UART2, WiFi AP+STA, OTA self-update on the `/ota/v2` channel. Main screen: top
bar (WiFi/data/FX tiles + DEV tile when the server mode is DEV, sat count right),
then row 1 = instant speed + a square bearing-ring cell, row 2 = avg+max,
session time, wind and bearing, plus a hint spine down the right edge naming each
of the three buttons.

Board-level facts (variants, wiring, flashing, port matrix) live in
`DEVICE_NOTES.md`; this file is behaviour.

## Hardware / toolchain

- **ONE board since 2026-11-01: ESP32-S3 N16R8** (16 MB QIO flash + 8 MB OPI
  PSRAM, `default_16MB.csv` dual-OTA). The classic-ESP32 (`esp32dev`) env and its
  buildchain were retired; `platformio.ini` keeps the `[env]` + `[env:esp32s3]`
  structure so a second variant later is a block, not a refactor, and `PIOENV`
  defaults to `esp32s3` in the Makefile. `include/board_pins.h` carries the pin
  map and `include/User_Setup.h` the display setup (`-include`).
  `make compile|upload|monitor|clean`, e.g. `make upload PORT=COM3`.
  - The S3's only USB is the native CDC/JTAG: the env sets
    `-DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1`, without which `Serial`
    goes to the unconnected UART0 pins and `/serial` stays empty. A dual-USB S3
    devkit also exposes a **CH343 UART bridge** on its other Type-C port: that one
    flashes fine but shows only ROM/panic output, because the app's `Serial` is
    the native CDC. Panics still land there (ROM prints them on UART0).
  - **`-DUSE_FSPI_PORT` in the v2 env is LOAD-BEARING.** Without it the app
    boot-panics in a loop on the first `screenInit()`:
    `Guru Meditation ... StoreProhibited, EXCVADDR 0x000010`, backtrace
    `begin_tft_write` ← `TFT_eSPI::init` ← `Canvas::init` ← `screenInit`.
    TFT_eSPI 2.5.43's S3 path sets `SPI_PORT = FSPI = 1`, and IDF's
    `soc/esp32s3/include/soc/soc.h:122` defines
    `REG_SPI_BASE(i) = (((i)>=2) ? DR_REG_SPI2_BASE + (i-2)*0x1000 : 0)`
    — so `_spi_user = SPI_USER_REG(1)` = `0 + 0x10` and the first
    `writecommand()` stores to address 0x10. The library's own
    `#ifndef REG_SPI_BASE` workaround never fires because IDF *does* define it.
    Forcing `SPI_PORT` to 2 selects GPSPI2, the bus the `SPI` object really
    uses. v1 is unaffected (`VSPI` = 3 is a valid base). This is IDF-version
    independent — it is NOT a core 2.x-vs-3.x problem.
  - **S3 pin traps, and the per-variant map** (`include/board_pins.h`, chosen by
    `-DDEVICE_VARIANT_V2`; v1 keeps exactly the field wiring). On the S3: GPIO
    **22–25 do not exist** (S3 = 0–21, 26–48), **19/20 are the native USB pads** —
    the v1 boot chime on 19 grabbed them and killed the USB console/OTA link
    while the app kept running (the portal still served, which is what made it
    look like a dead board) — **26–37 are the QSPI flash + OPI PSRAM bus**, and
    0/3/45/46 are strapping. v2 therefore uses buttons **21 / 38 / 39** (a third
    front button exists on v2, with no UI action bound yet; 40/41 free),
    buzzer **47**, GPS 16/17 and the display pins unchanged. `Button::Third` is
    in the enum and the table is built from the macros, so v1 (whose
    `PIN_BTN_THIRD` is -1) compiles the two-button table. `POST /btn` takes
    `b=L|R|3`.
  - **Display config**: `include/User_Setup.h` (ST7796_DRIVER 480×320 — the panel
    is 320×480 native, drawn at rotation 1,
    CS 10 / MOSI 11 / SCLK 12 / DC 2 / RST 4 — the SPI2-native pins, because
    **the S3 has no GPIO 23**, and driving it produced a dark panel plus two
    `Invalid pin selected` boot warnings). Rotation is `TFT_ROTATION 1` in
    `User_Setup.h` → 480×320 landscape. The **MAIN screen is laid out for
    480×320** (module list below); `screen_race.*`, `screen_diagnostics.*` and
    `screen_config.*` still paint the legacy 320×240 frame, so those pages sit in
    the top-left corner until they are ported — the panel, driver and geometry
    are correct, only that frame is left. No backlight control: no `TFT_BL`, the
    panel's LED is tied to 3.3 V.
  - **OTA is split per variant** (`OTA_CHANNEL` in `config.h`, used by
    `ota.cpp`): v1 pulls `/ota`, v2 pulls `/ota/v2`, and `make dist
    PIOENV=esp32s3` stages into `public/ota/v2/`. One shared channel would let a
    v2 board pull the v1 (esp32dev) image — wrong chip, no boot, and nothing to
    recover with once the enclosure is sealed. `/status` reports
    `sys.variant`, `sys.otaChannel` and `sys.ap`. **Bootstrap caveat:** a board
    whose firmware predates the split still asks the legacy `/ota` path, so its
    first update has to be staged there; after that it is on its own channel.
    Firmware 1.0.378 was the first v2 build; it was published to `/ota/v2` and
    pulled over TLS from Render on 2026-10-31, and `/ota` stayed on the v1
    1.0.375 image throughout.
  - **The AP name is per board**: `IndietroTutta-XXXX`, XXXX = the last two bytes
    of the STA MAC (`wifi_manager.cpp: wifiApSsid()`), i.e. 14:C1:9F:CD:6D:FC →
    `IndietroTutta-6DFC`. Two unsuffixed boards in range both advertised
    `IndietroTutta`, so a laptop joined whichever answered first and the portal
    could drive the wrong boat with no error.
- Arduino, libs: `TFT_eSPI + TinyGPSPlus + ArduinoJson@^6.21.1`.
- Display ST7789 CS5 DC2 RST4 MOSI23 SCLK18, rotation 3, `TFT_BGR` in `platformio.ini`.
  Pins in `include/User_Setup.h` force-included via `-DUSER_SETUP_LOADED -include`.
- Buttons GPIO21=Left GPIO22=Right (v1); the map lives in `include/board_pins.h`
  (`PIN_BTN_LEFT/RIGHT/THIRD`), active-low, 50ms debounce, 500ms long-press.
- Buzzer `PIN_SOUND` (`sound.h`): 19 on v1, **47 on v2** — never 19/20 on an S3.
- GPS UART2 RX16 TX17 @9600, parsed by TinyGPSPlus in `gps.cpp`.
- **Power: 3.3 V buck-boost, NOT a 5→3.3 V linear regulator (changed 2026-10-21).**
  The original on-board 5→3.3 V regulator fried mid-race and took the device with it
  (session 230: quiet at 10:21:51Z, then a clean reboot with RAM lost and no clue in
  the serial). It got hot in normal use. **A device that dies and comes back rebooted
  is a POWER suspect first** — brownout/reset, not firmware — especially under display
  + Wi-Fi + OTA load; check the regulator/buck before chasing a firmware bug, and do
  not read a lost serial ring as evidence of a firmware fault.
- Build: `make compile` (must be SUCCESS before commit), `make upload PORT=COM5`,
  `make monitor`, `make dist` (= shorthand `dc`: bump + publish OTA), `make dl`
  (= shorthand `dl`: ota-local + device pulls immediately, `DEVICE_IP ?= 192.168.0.106`
  but the IP is DHCP — pass `DEVICE_IP=...`). Toolchain: `.venv` PlatformIO.
  Shorthands `sl`/`sc`/`dl`/`dc` and the working modes `devl`/`devc` are defined in the
  root `AGENTS.md`.

## Architecture — superloop + backend task

`src/main.cpp loop()`: serial_buffer → buttons → gps → screens → wifi → backend → ota.
Two FreeRTOS tasks in `backend.cpp` (core 0): feed task (12288 stack; mock poll
+ telemetry upload + deletes + course fetch; slow passes >2s logged with leg
split) and health task (18432 stack; heartbeat 30s idle / 5s live + run upload,
which needs the extra room for its 2KB JSON body) so a slow `/health` never
starves the GPS feed.

**A retry must never spin (learned the hard way, 2026-11-02).** A transient
`POST /gps` failure requeues the fix at the FRONT of the queue, and the only
wait in that loop is the queue *receive* timeout — which is skipped while an
item is queued. With no backoff that is a tight loop of connect+log+retry: it
outruns the IDLE task on core 0 and the **task watchdog reboots the board**
(`/status` → `sys.reset: "task-wdt"`), while flooding `/serial` with identical
lines so the ring holds no evidence of anything. It bit twice on one race: a
race finish adds the run-upload connection, a couple of position sends fail,
and 2 failures became thousands. `positionFails` now backs off 0.5s→15s, yields
with `vTaskDelay`, and logs only the edge (`failed (n)`) and the recovery. Any
new retry path needs the same discipline.

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
- `serial_buffer.cpp`: 200-line mutex-guarded log for `/serial`, plus a ~1KB
  **RTC-memory mirror** of the same lines. RTC RAM survives a panic, a watchdog
  and `ESP.restart()`, so the tail is replayed at the next boot as `[PREV] ...`:
  a heap ring dies with the very reset it would have explained.
- `boot_diag.cpp`: `esp_reset_reason()` read first thing in `setup()` →
  `[BOOT] reset=… boots=… panics=…` + `/status` `sys.reset|boots|panics`
  (NVS ns `diag`). `power-on` / `software` (our own restart) / `panic` /
  `task-wdt` / `brownout` is the first question to ask of any mystery reboot.
  `/status` `mem` also carries `loopStack` / `backendStack` / `healthStack`
  high-water marks (bytes free at the worst moment since boot).

Key modules: `screens.*` router + 200ms throttle, `screen_speed.*` main —
**480×320** (v2, the only board since 2026-11-01), geometry given in mm at
6.49 px/mm. Top bar `TOP_H` 30 px (4.6 mm, rule at y=29; WiFi/data/FX/DEV tiles
left, sat icon+count right-aligned against the hint spine — the icon rides 2 px
above the digit line). Row 1 (`ROW1_H` 188 px = 29 mm): instant speed cell
(`SPEED_W` 263: "Speed (kn)" label at `SPEED_LABEL_Y`, segmented value that
re-fits its font and height to the cell, per-glyph diff + one-off wipe on a
width change) | SQUARE bearing-ring cell (`BRG_W` 188, dial `cmpR` 86 = o172,
inscribed with an 8 px margin). Row 2 (`ROW2_Y` 218, 102 px): four cells —
avg+max | session time (136 px: the clock needs it, "12:34" is 123 px) | wind |
bearing — a font-6 VALUE with its font-2 label **UNDERNEATH it** (`ROW2_VALUE_Y`
266, `ROW2_LABEL_Y` 292), and the wipes keyed to `ROW2_VALUE_TOP`, never to the
label. Cell 1 is the exception (`3S` scope toggle): avg and max are **STACKED**,
one font-4 (26 px) value per line right-aligned at `AVGMAX_VALUE_R`, each with its
own font-2 caption at the cell's left edge (`AVGMAX_ROW1_Y` 248 / `AVGMAX_ROW2_Y`
290), and each line wipes only itself (the average moves every fix, the max
rarely). Right edge: a 29 px (4.5 mm) **hint spine** running the FULL panel height,
cut into three cells at the panel thirds — one cell per physical button, turned
90° (spine text: letters on their side, read top to bottom), short press WHITE
over long press **orange** (`HINT_LONG` 0xFD20). Words are page-specific and
painted by `hintBarCell()` from the 200 ms pass — it change-detects and repaints
one cell only, so no `requiresInit` bookkeeping is needed, but a FULL-SCREEN
clear (page switch, `/screen` grab, the race menu's `fillScreen`) must call
`hintBarReset()` or the warm cache leaves a blank column. MAIN: `RACE CFG` /
`BEAR DIAG` / `PARTIAL` (`ALL` when a partial is running), RACE: `MAIN MENU` /
`VIEW BEAR` (and `MENU` alone when there is no session), `RACE OPTIONS` menu:
`BACK` / `SELECT` / `PICK`, course picker: `BACK` / `NEXT` / `PICK`, course
options: `ROW`+`BACK` / `CHANGE` / `START`, CONFIG: `BACK OTA` / `SELECT` /
`TOGGLE`. **The renderer advances by each glyph's MEASURED width**
(`spineCharW`/`spineLen`): a fixed 8 px step out of an 8 px sprite shaved the wide
font-2 letters, so `MENU` came out with its stems cut — the sprite is now 16 px
and the box length is the word's own advance.
Ring cell (spans both rows, `cmpCx` = `SPEED_W` + 94, `cmpCy` = `BODY_TOP` + 94):
a **2 px** ring line (TFT_eSPI has no pen width, so two adjacent circles at r and
r-1 — `drawCompassCircle()`), 30° radial ticks that are TRUE-BEARING marks and
therefore rotate with the card like the N (`d + nDeg`; length class keyed off the
ABSOLUTE d), the font-2 "N" at `r - RING_BAND - 8` (flush against the no-go band,
WHITE), two solid triangles sharing the ring tangent (green boat tip at r-1, red
wind tip downwind = windDir+180, 16°/side base), the no-go band (RING_BAND 12 px,
`TFT_RED`, width via `/nogo`) and the centre readout under a small WHITE "Wind"
caption (font 6 boat/wind angle + drawn degree dot; 0 = dead upwind). Zero
flicker rule: every element is change-detected on its own angle, erased in BG at
its PREVIOUS geometry and repainted in z-order band → wind → boat, with damage
propagation in both directions (a moved shape drags its neighbours and the band
back in) — see the block comment before the gate.
`screen_race.*` shared RACE/PRAC screen: **body = the panel minus the hint spine**
(`MAP_W` = `HINT_BAR_X` 451 since 2026-11-04 — the map, its frame, the 3-cell
header grid and every clip derive from `MAP_W`, so nothing paints over the
spine). Header = one font-4 row DST | SPD | TIME, plus the 30 px alert banner
over the map's bottom edge; map = wireframe +
boat triangle/edge-dot + ~1 cm dashed yellow stub toward the next mark (2/3 the
length of the COG stub) + a cyan dashed stub along the boat's own COG (`gps.course`,
only above 0.5 kn, clipped to the map rect — it can point into the header band);
mark numbers drawn above the circles; view tag; empty state clears the body and
shows a 3-line message; map ≤1Hz, text padded. Spine words (`raceHints()`):
`MAIN`/`MENU` and `VIEW`/`BEAR`, cell 3 empty (no action on this page) — and
**with no session the only cell is `MENU`, alone and in the long-press colour**,
because it is the one thing an empty page can do; `2S` is a hard no-op there (it
used to force a redraw of the empty state, which read as a flicker),
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
`canvas.*` drawing-target dispatcher: `tft` is a `Canvas` object, not a
TFT_eSPI reference, so every call goes either to the panel (`gPanel`) or -
during a `/screen` capture - to the 320x40 band sprite (`gCapture` +
`gCaptureY`, y translated). The ST7789 is write-only so a sprite is the only
readable surface; with an explicit dispatcher an un-forwarded method is a
compile error, not a silently incomplete grab (`canvasPushImage()` is now just
a forward; font 1 on a sprite faults, so capture uses font 2 for the race pane
labels),
`config_store.*` NVS ns `wifi` blob `cfg` + `wifi_%d_ssid/pass`.
(Race engine now lives in `race_session.*` + `race_run.*` + `race_courses.*` + `screen_race.*`.)
**The race clock base is the GUN, never the line crossing:** every start path in
`race_run.cpp` sets `startEpoch = gun`, so the panel clock, `splits[]` and the
uploaded `elapsedSec` are all gun-relative (a late or OCS-returning start costs
what it lost). The START **event** still lands on the crossing — that is what the
spec scores.
**Mark-passing law: `SPEC-mark-passing.md` at the repo root — LOCKED, read it
before touching any pass/score/target logic.**

## UI gestures — `1S`/`1L`, `2S`/`2L`, `3S`/`3L`

**Naming (from 2026-11-01): button number + press length.** `1` = left button,
`2` = right, `3` = third; `S` = short press, `L` = long press (500 ms). So `1S`
is left-short, `2L` is right-long. **Use this vocabulary everywhere** — code
comments, the portal interface (`POST /btn?b=2&e=L`), bruno, docs, bug reports.
The old `L`/`LL`/`R`/`RR` is retired; `/btn` still accepts it as a legacy alias
for old scripts, nothing else.

- `1S`-cycle = MAIN → RACE → MAIN (`PAGE_CYCLE=2`; the WAYPOINTS page was REMOVED
  on `feature/remove-waypoints` `9e55584`, flagged-post/delete plumbing
  included). DIAGNOSTICS + CONFIG excluded.
- MAIN: `1S` next, `1L`→CONFIG, `2S` cycles the ring view **north-up →
  heading-up → wind-up** (RAM; heading-up puts the boat's own COG at the top,
  wind-up puts the wind SOURCE at the top so the no-go arc is centred on 12
  o'clock; the wind arrow, the arc and the N glyph all rotate by the same
  offset, and a view whose input is missing — no COG, no wind — is skipped by
  the button and drawn as north-up until the input returns; the view in effect
  is named in the cell's top-left corner `North`/`Head`/`Wind`, with
  `/status` `sys.ring` reporting mode vs drawn),
  `2L`→DIAGNOSTICS. RACE: `1S` next, `2S` cycles the **view** Map → Target →
  Auto (**no-op with no session** — there is no picture to re-frame), `2L`
  toggles the **bearing** north-up ↔ heading-up, `1L`→MENU.
  DIAGNOSTICS/CONFIG: `1S` back to MAIN.
- **Terminology, race screen — canonical, use these words in code comments,
  docs and bug reports**:
  - **`2S` = view mode.** Three states, **Map → Target → Auto**.
    **Map** = the whole course fitted to the window — *overview* is a synonym.
    **Target** = the next destination centred at a fixed zoom — *focus* is a
    synonym. **Auto** = Target engages by distance (`autoDistM`,
    10/50/100/200 m, NVS) and **holds the zoom on the mark just rounded** until
    the boat is clear of it, then falls back to Map for the new leg. Code:
    `focusMode` 0/1/2, NVS `race`/`viewMode`.
  - **`2L` = bearing mode.** Three states, **north-up → heading-up →
    wide**. **North-up** = true north up (`N`). **Heading-up** = the direction
    toward the next destination is up (`BRG` — it is
    `courseTo(boat, destination)`, the *bearing to the mark*, not the boat's
    COG; the two agree while you are steering at the mark, and the mark's
    bearing is what the chart should point at). **Wide** (todo 48) = the
    course's own orientation: `wideRotation()` tries every 5° and keeps the fit
    with the largest scale, so the whole course stays on screen while the
    picture grows to fill the window (on a N-S course in the 320×150 frame that
    is twice the zoom of the north-up fit). In Target/Auto framing the span is
    fixed, every angle scores the same, so wide resolves to north-up. Code:
    `viewMode` 0/1/2, RAM only.
  - The two axes are **orthogonal**: any view can be north-up or heading-up,
    and the hint shows the states **in effect**, not the next action.
  - **Do not say** "focus view" for the bearing axis, "overview" for a fourth
    mode, or **"FIT"/"best-fit"**. That dead third state (`viewMode == 2`,
    0°/90° whichever filled the screen, left unreachable by the old `% 2`
    cycle) is now implemented properly as **wide** — use that word.
  - Naming trap: in `screen_race.cpp` the code's **`viewMode` is the bearing
    axis** and **`focusMode` is the view axis** — the opposite of the words
    above. Read the enum, not the name.
  - Main-screen ring is a different thing with confusingly similar labels: its
    `2S` cycles north-up → heading-up → **wind-up**, where heading-up puts the
    boat's own COG at the top and wind-up puts the wind SOURCE at the top (so
    the no-go arc spans 12 o'clock). The race screen's heading-up points at the
    *destination*.
- RACE MENU **`RACE OPTIONS`** (renamed from `RACE/PRACTICE MENU` 2026-11-04) —
  rows are BUILT from live state
  (`buildMenuRows`), so you can never be offered an action that makes no
  sense: **Start** only with no session, **Repeat** only when a course is
  remembered and the session is not a committee race, **Resync** only in a
  race, **Abandon** only with a session. `2S` walks the rows, `3S` picks, `1S`
  backs out. It is a **CONFIG-style table** — label left at `MENU_LABEL_X` 18,
  value right-aligned at `MENU_VALUE_R` 433 (`Auto distance` is the only row
  with one) — and it carries its own spine: `BACK` / `SELECT` / `PICK`.
- PRACTICE SETUP: Start → course picker (`2S` next, `3S` choose, `1S` back;
  pre-selected on the remembered course, else Windward-Leeward) → **Options**
  (`2S` cycles the highlighted row's value, `1S` moves between the two rows,
  `1L` back to the list, `3S` start). Gun 10/30/60/120/300s, line 10/20/30m;
  the line sits `distM` metres UPWIND of the boat. Values + course are
  remembered in NVS (`race` ns `prefCourse`/`prefGun`/`prefDist`) and are
  written **only on confirm**, so browsing never changes a default. The picker
  and the options are **full-screen pages in `RACE OPTIONS`' own chrome** (navy
  title bar, the same label/value table, the same spine) — `drawTableTitle()` +
  `drawTableRow()` — not a box floating over the frozen map, so pressing Start
  never drops the user into a different-looking world. The picker's `1S`/`1L`
  **returns to `RACE OPTIONS`** (`courseBackToMenu()`), not to the map: that is
  where the setup was entered from and the race page has nothing to add while it
  is unfinished. Spine: picker `BACK`/`NEXT`/`PICK`, options
  `ROW`+`BACK`/`CHANGE`/`START`.
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
- CONFIG: `R` select row, `RR` apply, `LL` force OTA now. Seven rows in **both**
  server modes — `OTA on boot, Server, Speed unit, Key beep, Boot sound, Gun cue,
  FX source` (todo 51 removed the PROD-only hide of the last one, and a mode
  switch no longer forces the fix source back to REAL). Rows are drawn from
  `ROW_COUNT`, so an index needs no remapping; there is no shrinking list.
- Every page switch full-black clear; ghost-clear readouts in speed.
- NO fillRect/fillScreen/clear on the 200ms refresh path — it flickers. Overwrite
  text in place with space padding instead, e.g. `" " + val + " "` (both sides
  for MC_DATUM, leading space suffices for TR_DATUM). Static labels can just be
  redrawn with the same string. Full clear only on page switch / init / mode toggle.
  `drawSmart` helps: it remembers each (x,y,datum,font) it wrote and erases the
  **union** of the old and new boxes, so a **centred** string that shortens
  (`GUN 1'00''` → `GUN 59''`) cannot leave its head behind — MC_DATUM moves on
  both sides, which a right-tail erase never covered (todo 50). Same reason
  `drawReadoutC()` exists instead of the old edge-anchored readouts.
- `/screen` re-renders cleanly, so it HIDES incremental-redraw ghosting (stale pixels,
  missing grid/labels) — verify that class of bug on the PANEL, not the grab. Font 1
  (GLCD) drawn into a `TFT_eSprite` faults intermittently → small labels use
  `drawLabel1()`/`drawLabel1C()` in `canvas.h` (manual GLCD via `drawPixel`).
  A capture re-renders the CURRENT page with `requiresInit`, which has one side
  effect that bites scripts: the CONFIG screen resets its highlight to the first
  row, so count `R` presses from `OTA on boot` after every grab.
- **A capture desynchronises the PANEL from the change-detect caches, so every
  grab used to plant a ghost arrow.** `initScreen()` resets `prevBoatDeg`/
  `prevWindDeg` to -2 while the capture draws into a sprite and never touches the
  panel; the next 200 ms pass then finds `prevBoatDeg < 0`, SKIPS the erase and
  paints the new arrow beside the old one — permanently, because from then on
  prev is the angle drawn, not the angle on screen. `handleScreen()` therefore
  calls `redrawCurrentPage()` when it finishes (and on a truncated capture): the
  panel blanks for <=200 ms and repaints, so what is on screen always matches the
  caches. Anything that resets the state without repainting inherits the same bug.
- Race screen states: **empty / pre-start / racing / finished**. On the finish
  edge the screen snapshots the result (todo 38): `FINISHED` in green in the
  banner slot and the elapsed time frozen in the header, both held **after the
  backend drops the finished session** (it returns `session:null` seconds after
  the run uploads) until a new run/session arrives or the page is left
  (`raceFinishHoldClear()` from `screens.cpp`). The distance readout blanks while
  held, and the empty state does **not** repeat the word: the `FINISHED` banner
  already says it and the header holds the time, so only the "start a practice
  session from menu / or await committee instructions" lines are drawn (centred
  where the old `Race finished` headline sat). Banner precedence: signal (8 s) →
  FINISHED → countdown → OCS → transient.
- Race screen overlays (**`RACE OPTIONS`, the course picker and that course's
  options**) are the SAME page in three faces: navy title bar, a column of
  label/value rows, the hint spine — and **no `>` cursor** (the yellow row is the
  selection). A row repaints **itself** through `drawTableRow()` and only when its
  text or its selection changed, so walking the highlight is a two-row touch-up,
  never a rows-band fill (which read as a flash on every press); `drawTableTitle()`
  is the only thing that clears, and it resets the row **and** spine caches
  (`tableRowsForget()` + `hintBarReset()`), because `fillScreen` wipes both.
  `clearTableRowsFrom()` wipes rows a shorter page no longer uses through the same
  change-detect. A page-level note must be drawn **after** the rows: a row wipe is
  a band fill and erases anything in the band below it.
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
- **The target highlight and the target destination are TWO code paths and must
  stay in lockstep** (todo 49). `raceTarget()` paints the ring/line highlight;
  `nextDestination()` paints the destination stub, the pane distance and the
  heading-up bearing. Both must answer the same four questions: pre-start =
  start, racing = `marks[progIdx]`, **every mark scored = the finish LINE**
  (armed — only a line cross ends the run), finished = finish.
  `raceTarget()` was missing the third case, so after the last mark it clamped
  to `markCount - 1` and the ring sat on the last mark for the whole final leg
  while the stub, the `FINISH` tag and heading-up had already switched. Any new
  target state goes into both functions.
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
- **Every map segment is clipped to the rect the map owns** (todo 36).
  `drawStaticLayer` projects the course in whatever view is active, and the
  projection is **not** bounded by the window — Target forces a 0.0003° span,
  heading-up rotates the fit box — so an unclipped leg is a page-long chord
  across the header readouts, the frame and the hint bar. `clipToMap()` runs
  Liang–Barsky against `MAP_X .. MAP_X+MAP_W-1` × `WIRE_TOP .. WIRE_Y1-1` and
  returns false for a segment wholly outside; wide lines pass `inset` 1-2
  because they are drawn centred on the segment. `circleOnMap()` (+12 px: the
  round arrows stand outside the ring) culls marks, their labels and the
  deferred target ring — a circle centred in the header band is not clipped by
  the rect, it **paints** there. The boat's destination stub is clamped with
  `clampToWire()` like the COG stub. Any new map geometry goes through one of
  the two.
- **The dirty map path is only valid while the CAMERA is unchanged** (todo 46).
  `drawStaticLayer()` *paints but never clears*, so it is only "identical
  pixels" for a fixed projection. The map repaints fully only on a session
  change (`mapFull = mapDirty || courseChanged`), so a camera move with no
  session change (Target view re-centres on each mark switch — the span is
  fixed, the origin moves — and `focusEnabled` never toggles there) would leave
  the previous projection's labels, circles and legs standing. `drawRaceMap`
  therefore fingerprints the camera (`p.lat0/lon0` in screen px + `p.scale`)
  and forces one full repaint when it moves. **The rotation test must run
  BEFORE the fingerprint and gate it** (`camMoved && !rotating`): the fit origin
  is computed in rotated space, so in heading-up the fingerprint otherwise
  promotes every pass to `full` and the 1 Hz rotating throttle never runs — a
  full wipe at the 200 ms pass rate, which is what "heading-up flickers" was.
  Its epsilon is **2.0°**, not 0.1 (0.1 is under GPS noise: 2 m of jitter 30 m
  out swings the bearing ~4°), so a boat holding a course does not re-fill the
  map at all. Mark labels are also clamped to `WIRE_TOP + 9`: anything drawn
  above the map's clear rect can never be erased again (the hole todo 35
  found).
- **A REPAINT MUST NEVER RESET THE RUN** (todo 47). `requiresInit` is true for
  every `redrawCurrentPage()` (R, closing a menu, OTA), and `raceRunStarted()`
  only means "the START was **scored**" — so gating a reset on
  `!raceRunStarted()` wiped the whole pre-start memory
  (`gunSide`/`hasGunSide`/`lastSide`/`approachSide`/`ocs`) on any redraw before
  the crossing. The clean-crossing test needs exactly that memory, so the boat
  could not score its start and fell to the 60 s "late join" escape. Session
  changes own the reset (`courseChanged && key != lastResetCourseKey`); a
  repaint owns none.
- **The GO/OCS banner is an EVENT, not a gun cue** (todo 47). It is raised by
  `raceGunEdge()` when `started` flips false→true, and it must stay that way:
  `noteGunEdge()` is reached only from a **real** start — the scored line
  crossing (`goCue`) or the point-start gun. The "late join" / "late cross"
  catch-up paths (no fix at the gun, GPS gap on the line) still set
  `started`/`startEpoch` — the clock is gun-based, so nothing is lost — and log
  the START event, but they must not banner: a discovery is not a start, and
  showing GO at gun+65 reads as "the race begins now".
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
- **`/screen` captures in 320x40 bands (fw 1.0.374) - it needs 12.8 KB of
  contiguous heap, not 77 KB.** The old capture was one 320x240 8bpp sprite: a
  single 76,801-byte `calloc` that TLS churn from talking to **Render** fragmented
  the heap below (~40,948), so `GET /status` stayed fine while `/screen` answered
  `503 no memory for capture` - and every grab failed until a reboot, which is
  exactly when UI work was being verified against prod. Now the page is re-rendered
  one 320x40 band at a time and the bands are streamed in order; the wire format is
  unchanged, so `scripts/grab_screen.py` and the PNG pipeline needed nothing. Read
  `/status` first if a grab ever misbehaves: `mem.maxAllocHeap` beside
  `screen {bandH, bands, ok, lastMs}` (`bands: -1` = a band failed to allocate and
  the response was truncated). The grab is still an **8bpp RGB332** sprite: colours
  are quantised (e.g. `RDIM` 0x8410 comes back as a nearby palette value), so a
  pixel probe must match by tolerance, never by exact RGB565. Grabs are written
  to `../grab/` at the project root (`scripts/grab_screen.py`): a bare filename
  lands there, and with no filename at all the file is `<host>_<page>_<stamp>.png`.
  The pre-band reference set for todo 6 lives in `grab/ref6/`.
- Portal always up: open AP **`IndietroTutta-XXXX`**, DNS → `192.168.4.1` → `/config`.
  XXXX = the last two bytes of the STA MAC (`wifi_manager.cpp: wifiApSsid()`),
  i.e. the tail of the same address the backend knows the board by
  (14:C1:9F:CD:6D:FC → `IndietroTutta-6DFC`), and `/status` reports the live
  name in `sys.ap`. The suffix exists because two un-suffixed boards in range
  both advertised `IndietroTutta`: the laptop joined whichever answered first
  and the portal could drive the wrong boat without any error.
  Routes: `/config /save /wifi/remove /reset (wipe all!) /reboot /status /health /serial /mock?on=1|0 /nogo (GET current; POST /nogo?deg=<total width 10..180> — no-go arc width, own NVS key, shown in /status) /ota (POST immediate check vs current server) /server?mode=prod|dev&host=<ip:port> (GET=current) /btn?b=L|R&e=R|RR (remote button) /screen (RGB565 BE 320x240 grab: re-renders the current page into a 320x40 8bpp band sprite at a time and streams the bands in order (12,800 bytes each), since the ST7789 can't be read back; `scripts/grab_screen.py` → PNG)`. All unauthenticated.
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
- **`sys.ring` in `/status`** (`{mode, drawn, canHeading, canWind}`): the main
  ring's view state. `mode` is what `2S` selected, `drawn` is what the dial is
  actually showing — they differ **exactly** while the mode's input is missing
  (no COG / no wind), and that difference is invisible in a screen grab (the dial
  just looks north-up), so it is unguessable from a photo. `canHeading`/`canWind`
  are the flags the button skips an undrawable view on. Read this instead of
  counting presses: a press is indistinguishable from a finger on the button
  when someone is standing at the board, and the third state is skipped outright
  when there is no wind to show.
- **Diagnostic logging is rationed on purpose.** The 200-line serial ring
  rolls in ~3 minutes and a race does not fit in it. The start-window trace
  logs only on a CHANGE of side/near (~4 lines per race); an earlier
  once-a-second version pushed `passed mark` and `finished` out of the ring
  — the diagnostic destroyed the evidence it was added to collect.
- OTA: `GET <otaBase>/latest.txt` → semver compare → `HTTPUpdate firmware.bin` + progress bar + `redrawCurrentPage()`, where `<otaBase>` = `serverBaseUrl() + "/ota" + OTA_CHANNEL` (`""` on v1, `"/v2"` on the S3). `rebootOnUpdate(false)`: on success the panel gets a clean `fillScreen` before `ESP.restart()` so the boot splash shows neatly (no stale progress-overlay pixels). Boot check if `otaCheckOnStart`, 60s WiFi timeout. `make ota-local` stages a dev build into LAN `public/ota$(OTA_SUBDIR)/` with no commit/push/hook (cloud untouched; `config.h` + `public/ota/*` stay dirty by design); `make dl` (= `dl`) = ota-local + immediate pull + verify, and `make dist` (= `dc`) is the cloud publish that commits the bin into the backend repo — **always pass `PIOENV=esp32s3` when publishing v2**, otherwise the v1 channel gets the image.
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

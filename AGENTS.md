# AGENTS.md — IndietroTutta device firmware (ESP32)

Portable marine GPS instrument: ST7789 240×320, 2 buttons, GPS UART2, WiFi AP+STA,
OTA self-update. Main screen is always big `speed` + right column (Max/Course/Session grid).

## Hardware / toolchain

- Board `esp32dev`, Arduino, libs: `TFT_eSPI + TinyGPSPlus + ArduinoJson@^6.21.1`.
- Display ST7789 CS5 DC2 RST4 MOSI23 SCLK18, rotation 3, `TFT_BGR` in `platformio.ini`.
  Pins in `include/User_Setup.h` force-included via `-DUSER_SETUP_LOADED -include`.
- Buttons GPIO21=Left GPIO22=Right, active-low, 50ms debounce, 500ms long-press.
- GPS UART2 RX16 TX17 @9600, parsed by TinyGPSPlus in `gps.cpp`.
- Build: `make compile` (must be SUCCESS before commit), `make upload PORT=COM5`,
  `make monitor`, `make dist` (bump + OTA publish). Toolchain: `.venv` PlatformIO.

## Architecture — superloop + backend task

`src/main.cpp loop()`: serial_buffer → buttons → gps → screens → wifi → backend → ota.
No RTOS tasks except `backend.cpp` FreeRTOS task (12288 stack, core 0):

- UI thread only snapshots GPS + `enqueueWork()` (16-deep queue, drops oldest).
  Waypoints carry a device-generated `uid` (`wp-<millis>-<seq>`, stored on the
  marker); deletes go through an 8-deep uid queue (`DELETE /gps/flagged`,
  retried until sent/gone).
- Task drains queue → `POST /gps`, plus `GET /health` every 30s.
- `backendLoop()` adaptive GPS throttle: `30s@0kn → 2s@5kn` linear (`gpsIntervalForSpeed`).
- `backend.h: backendOnline()`, `backendSendFlaggedPosition()`, `backendInit/Loop`.
- `serial_buffer.cpp`: 200-line mutex-guarded log for `/serial`.

Key modules: `screens.*` router + 200ms throttle, `screen_speed.*` main
(big speed nudged right of center; right column MAX/CRS/SES — gray font-2 labels,
white font-4 values, redrawn only on change, width-capped; POS line removed),
`screen_race.*` shared RACE/PRAC screen (frame = all but header/hints; wireframe +
boat triangle/edge-dot + dashed yellow to destination center + wind arrow;
GPS countdown header + next-tag; strip with mode tag + next data + N-UP/BRG/FIT;
map layer ≤1Hz, text padded),
`race_session.*` health-pulled session cache (marks/lines/wind/startTime+offset,
NVS `race` ns, ArduinoJson heap doc; unassigned keeps cache),
`screen_waypoints.*` (LL flag, max 10 FIFO RAM-only), `screen_timers.*` chrono,
`screen_diagnostics.*` (RR from main), `screen_config.*` (LL from main),
`wifi_manager.*` non-blocking AP+STA (scan prefers visible strongest, park/retry),
`ota.*` semver vs `latest.txt`, `http_server.*` portal port 80,
`config_store.*` NVS ns `wifi` blob `cfg` + `wifi_%d_ssid/pass`.
(Race engine `race_store.*` removed 2026-10-06 — replanning from scratch.)

## UI navigation (hints: L/LL left, R/RR right)

- L-cycle = MAIN → WAYPOINTS → TIMERS → RACE → MAIN (`PAGE_CYCLE=4`). DIAGNOSTICS + CONFIG excluded.
- MAIN: `L` next, `LL`→CONFIG, `RR`→DIAGNOSTICS. RACE: `L` next, `R` resync health now (pull pushed session), `RR` cycles N-UP → BRG → FIT. DIAGNOSTICS/CONFIG: `L` back to MAIN.
- WAYPOINTS: `R` cycle, `LL` flag (also `POST /gps flagged:true`), `RR` delete.
- TIMERS: `R` start/stop, `RR` lap/reset. CONFIG: `R` select row, `RR` apply, `LL` force OTA now.
- Every page switch full-black clear; ghost-clear readouts in speed/timers.
- NO fillRect/fillScreen/clear on the 200ms refresh path — it flickers. Overwrite
  text in place with space padding instead, e.g. `" " + val + " "` (both sides
  for MC_DATUM, leading space suffices for TR_DATUM). Static labels can just be
  redrawn with the same string. Full clear only on page switch / init / mode toggle.
- `src/config.h`: `BASE_URL`, `OTA_BASE_URL` (both prod Render), `BUILD_VERSION 1.0.98`.
- Device reports fw: `Firmware-Version: BUILD_VERSION` header on `GET /health` +
  `POST /gps`, plus `"fw"` in gps JSON body.

## Network / portal / OTA

- `GET /health` headers `DeviceId:<MAC>` + `Username:`; response body parsed for
  `session` push (course + startTime + offset, cached in NVS); `POST /gps` JSON lat/lon/speed/course/alt/sats/flagged/username. `setInsecure()` everywhere, no auth.
- Portal always up: open AP `IndietroTutta`, DNS → `192.168.4.1` → `/config`.
  Routes: `/config /save /wifi/remove /reset (wipe all!) /reboot /status /health /serial`. All unauthenticated.
- OTA: `GET ota/latest.txt` → semver compare → `HTTPUpdate firmware.bin` + progress bar + `redrawCurrentPage()`. Boot check if `otaCheckOnStart`, 60s WiFi timeout.
- Bruno in `bruno/` covers portal routes (`access-point` + `local-network` envs).
- Quirks: empty portal name keeps stored username; username regex both sides; WiFi rotate-on-5s-fail never blocks UI; OTA download blocks loop; waypoints/laps RAM-only.

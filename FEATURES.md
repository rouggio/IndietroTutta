# Indietro Tutta — Features & Roadmap

> Persistent memory for the Indietro Tutta platform. Last updated: 2026-08-29 — Device 1.0.83, Backend Turso live.
> Stack: ESP32 + ST7789 + TinyGPSPlus + Turso libSQL + Express + Leaflet + Render OTA.

## Vision
Portable ESP32 regatta instrument + web platform. The device lives on `speed` — during a regatta or training session the same main screen surfaces the race: countdown (GPS-synced), next mark, distance/relative bearing, pass state and current position. No fleet projection, <10 marks, minimal screen cost.

Two modes, one engine:
- **Regatta Mode** — course drawn on the web, pushed as wireframe, start dispatched to all devices.
- **Solo / Training Mode** — sailor builds the course by sailing it, then races it with identical regatta rules and timing.

---

## 1. Baseline — Done
- **Device:** superloop, WiFi AP+STA non-blocking (12s timeout, park after 3 fails, definitive-failure probe), 200-line thread-safe serial buffer
- **Screens:** `speed` (main, always on), `waypoints`, `timers` in L-cycle; subscreens `diagnostics` (RR) + `config` (LL) with `L Back`, titles, hint bars `L/LL` left / `R/RR` right, ghost-cleared readouts
- **Main today:** big speed (kn/km/h/mph), course, top bar (fix/wifi/data + sats), `L Next LL Cfg | RR Diag`
- **Backend:** `GET /health` = heartbeat (30s, `POST` alias) → `upsertDevice` + `live(<90s)/idle(<10m)/offline`, Turso `devices` + `gps_points` (500 cap, `GET /gps?date=YYYY-MM-DD&deviceId=`), in-memory fallback, `.env` gitignored
- **Frontend:** Leaflet polyline + flagged markers, device list with status dot, calendar `Live ↔ past date`, auto-refresh 5s Live
- **OTA:** `make dist` bumps `BUILD_VERSION`, pushes `firmware.bin`/`latest.txt` to `public/ota`, triggers Render via `REDPLOY_HOOK_URL`

---

## 2. Design Principles
1. **Main is king, speed is always:** never hidden. Countdown, next mark, POS and alerts are asides on main.
2. **GPS time is the sync:** all devices share satellite UTC. `startTime` is absolute; `remaining = startTime - gpsTime` → zero network jitter. Fallback `serverTime + millis()` when no fix indoors.
3. **Minimal cost:** wireframe as `int32 lat/lon*1e5` (~300B for 8 marks), lines + circles only, no tiles, no fleet.
4. **Push early, count locally:** course + `startTime` dispatched well before `5:00`; device counts down locally.
5. **Offline resilient:** course cached in NVS/LittleFS, points queued when no WiFi.
6. **Template parity:** same 5 course templates exist on web and on device; same JSON, same engine.

---

## 3. Core Concepts

**Regatta > Session/Day > Race > Course**
- **Course:** ordered `marks[{lat, lon, radius, side:P/S/G, type:start/gate/mark/finish}]`, <10
- **Race:** `courseId, startTime (UTC), status: scheduled → live → finished → protest → final, participants[deviceId]`
- **Participant:** `userId + deviceId (MAC) + boat/class`
- **Signal:** `prec5, prec4, prec1, start, finish, alert, recall, abandon` piggybacked on `health`

**Wireframe payload:**
```json
{"v":1,"marks":[{"lat":3992511,"lon":965653,"r":30,"side":"P"},{"lat":3992600,"lon":965700,"r":30}]}
```

**Device poll:** `GET /health` returns `{race, course, startTime, signals, serverTime}` if assigned; otherwise solo. When `race.live`, poll signals every 5s.

---

## 4. Course Templates — Both Entry Points

Same JSON library, two UIs. See `FEATURES_courses.svg` for diagrams.

| # | Template | Order | Use |
|---|----------|-------|-----|
| 1 | **Windward-Leeward (W/L)** | `Start → 1 → 2 → 1 → Finish` | Training stick, default solo |
| 2 | **W/L with Gate** | `Start → 1 → Gate (2 buoys) → 1 → Finish` | Fleet gate practice |
| 3 | **Triangle** | `Start → 1 → 2 → 3 → Finish` | Classic |
| 4 | **WLT Olympic** | `Start → 1 → 2 → 3 → 1 → Finish` | Triangle + beat |
| 5 | **Trapezoid** | `Start → 1 → 2 → 3 → 4 → Finish` | Two-fleet |

- **Browser:** Course builder → `Templates` dropdown → drops marks centered on venue, square to wind, draggable.
- **Device:** `WAYPOINTS → Make Training Course → Templates` → generates marks around current GPS position oriented to current `COG` (wind proxy). `<10` marks. Sail to refine with `LL Flag`.
- **Rounding default:** single mark `P` (to port), gate `G` = either side valid; per-mark toggle `P/S/G` in builder.

---

## 5. Modes

### A. Regatta Mode (web-defined)
1. Organizer draws or picks template, sets order/radius/side, saves `courses` → attaches to `race`, assigns devices.
2. Sets `startTime` (e.g. `now+5:00`) → backend creates `prec5/4/1/start`.
3. Devices on next health poll cache `course v` + `startTime`, render thumbnail + main asides.

### B. Solo / Training Mode — Sail-to-Build
1. **Sail & flag:** `WAYPOINTS` `LL Flag` — sail intended track, flag 3-8 marks.
2. **Make course:** `TRAINING` subscreen lists flagged marks → order, pick `start`/`finish`, set `radius` (30m default), choose template or keep sailed order, save as `training_course v1` in NVS + `POST /courses` as personal course if WiFi.
3. **Race it:** trigger `1:00` countdown locally → same engine as Regatta: countdown, next mark, rounding detection, splits, total. `RR` to reset and re-run.

> After step 2 the device state is *identical* to a Regatta course — only the source differs.

---

## 6. Training Start Line — Spec

**One flag = line perpendicular to course, standard length sideways; next flag = windward end.**

- At `Flag 1` (`p1`):
  - If `speed > 2kn && hdop < 2.0 && course.isValid()` → `bearing = course + 90°`
  - Else **fallback:** wait for `Flag 2` (`p2`) then `bearing = bearing(p1→p2) + 90°`
- Store: `startLine {center: p1, bearing, halfLength: 40m}` → 80m total (configurable 30-50m)
- **Pass detection (line, not radius):** cross infinite line within `[center ± halfLength]` with 3m hysteresis + `speed > 1kn`.
- **Mark rounding:** `distance < radius` + leave on required side (`P/S/G`) → `PASSED`.

---

## 7. Device UX — Main Screen (both modes)

```
[ top bar: fix/wifi/data + sats ]
[ SPEED  12.3 kn ]                         — always, large, ghost-cleared, center
[  4:32  START  |  POS 39.9251 9.6566 ]    — countdown aside + current position indicator
[ Mark 2/7  127°  342m  [●/✓] ]             — distance + relative bearing + passed (replaces COURSE when race/training active)
[ alert banner if any — RECALL / OCS — 5s overlay ]
[ hint: L Next LL Cfg | RR Diag ]          — subscreens: L Back
```

- **POS indicator:** small text aside countdown + blinking dot on 40×40 wireframe thumbnail when fix valid; otherwise `POS --`.
- **Idle (no race):** countdown row collapses, `COURSE 127° (SE)` shown.
- **Passing a gate:** `●` = not passed, `✓` = passed; wrong side = stay `●`.

---

## 8. Features — Full List

### User & Device Management
- Auth magic link + password, JWT, RBAC Admin/Organizer/Sailor/Viewer
- Profiles: name, sailNo, boat/class, avatar; `deviceId ↔ user` pairing, transfer
- Teams/crews, invites, GDPR export/delete

### Regatta Management
- CRUD `regattas` (venue, dates, banner, public/private) → `sessions` (date, location) → `races` (startTime, course, participants, DNS/DNF/DSQ)
- Course builder: click/drag posts, order + radius + side `P/S/G`, distance auto-calc, GPX import/export, clone, **templates**

### Live Tracking & Safety
- Live map per regatta/session/race — filter by device, flagged-only toggle
- Auto rounding via geofence, safety board `live/idle/offline` + sats/hdop

### Rankings
- Low-point (RRS App A), high-point, custom; discards, tie-breakers
- Handicap ORC/IRC/Portsmouth or manual coeff; categories overall/class/youth
- Real-time leaderboard + provisional vs final; codes OCS/ZFP/SCP/RET

### Reports & Analysis (per Day/Session)
- Replay with playhead, flagged list, leaderboard snapshot
- Graphs per boat: speed/course/altitude/sats, VMG, distance, avg/max
- Head-to-head (2 boats), fleet heatmap, sortable stats, PDF/CSV/GPX export, share link

### Suggested — Weather, Notifications, Mobile/PWA, Offline Queue, Protests/Jury, Media, Platform Ops
- Wind/tide overlay, push/email 5-min warning, phone as backup tracker, ESP32 queued points, protest filing + jury recalc, photo attach, gallery, uptime metrics, audit log, API keys, Turso PITR

---

## 9. Backend Sketch

**Turso tables (next):**
```sql
users(id, email, name, role, sailNo, boatClass)
regattas(id, name, venue, startDate, endDate, visibility)
sessions(id, regattaId, date, location, notes)
courses(id, name, ownerId, marks JSON, version) -- templates are seed rows
races(id, sessionId, courseId, startTime, status)
participants(raceId, deviceId, userId, boat)
signals(id, raceId, type, at, payload)
-- existing: devices(deviceId PK, username, firstSeen, lastSeen), gps_points(...)
```

**API (additive):**
- `GET /courses/templates` → 5 presets
- `POST /courses` + `GET /courses/:id`
- `GET /race/active?deviceId=MAC` → `{race, course, startTime, signals}`
- `GET /health` now also returns `race/course` if assigned; `POST /races/:id/signal`

---

## 10. Incremental Plan — Holds Tight at Each Step

Each increment is shippable and testable on device + web + Turso. No increment breaks the previous.

### Step 0 — Templates Library (no behavior)
- **Do:** Add `courses/templates.json` (5 presets) + `GET /courses/templates`. No DB yet.
- **Test:** `curl /courses/templates` returns 5; frontend dropdown renders SVG.

### Step 1 — Course Cache + Wireframe Stub + POS (foundation)
- **Do:** Device: `course` struct, NVS cache `courseVersion`, `getSyncedTime()` (GPS > server+millis), 40×40 wireframe `drawWireframe()` (equirectangular, lines+circles), main `POS` line.
- **Test:** Hardcode a W/L course, see thumbnail + `POS 39.9 9.6` update, no crash when no fix.

### Step 2 — Countdown on Main (GPS-synced)
- **Do:** Backend: `courses` + `races` tables, `POST /races` with `startTime`. Device: `health` returns `startTime`, local `remaining = startTime - gpsTime`, main countdown slot `4:32 START` / `GO`.
- **Test:** Set start `now+2:00` from web, all devices count down in sync (compare two devices side-by-side, delta <1s).

### Step 3 — Next Mark + Pass (distance/bearing/✓)
- **Do:** Device: `nextMark` logic, `distanceToMark` + `relativeBearing = bearingToMark - course`, `PASSED` via radius + side `P/S/G`, advance on pass.
- **Test:** Sail to a 2-mark stick, watch `342m → 0m ✓` and auto-advance `2/2 → 1/2`.

### Step 4 — Training Sail-to-Build + Start Line
- **Do:** Device: `TRAINING` screen (flagged → ordered course), start-line fallback (`course` vs `p1→p2`), `halfLength 40m`, line-cross detection.
- **Test:** Flag start while drifting (fallback) and while moving (course), both produce valid line; run a training race and get splits.

### Step 5 — Web Builder with Templates → Push
- **Do:** Frontend: Leaflet course builder with draggable marks, side toggle `P/S/G`, save `POST /courses`, attach to race, assign devices.
- **Test:** Draw W/L Gate on web, push, device wireframe matches map within 5m.

### Step 6 — Signals + Alerts
- **Do:** Backend `signals` table + `POST /races/:id/signal`; device polls signals every 5s when `live`, main banner `RECALL` 5s.
- **Test:** Dispatch `recall` from web, device banner appears <30s (or <5s when live).

### Step 7 — Solo Re-race + Splits
- **Do:** Device: lap/split log per mark, `total + splits` on finish, `RR` reset; optional `POST /courses` upload of training course.
- **Test:** Re-run same training course, `best` vs `last` shows.

### Step 8+ — Auth, Rankings, Replay, etc.
- Only after 0-7 are green: add `users`, `regattas/sessions`, leaderboard, replay graphs.

> **Gate for each step:** `make compile` SUCCESS, `make dist` → Render live → device `health` + `gps` still OK → `FEATURES.md` updated. No step breaks `speed` or `health`.

Pick **Step 0** and we start with the JSON + endpoint.

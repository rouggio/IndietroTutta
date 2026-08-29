# Indietro Tutta — Plan of Action (Persistent Memory)

> This file is the session handover. Any new session should read this + `FEATURES.md` + `FEATURES_courses.svg` first.

## Current State (2026-08-29)
- **Device:** 1.0.83 at `192.168.0.106` (`Ciccio`), `health` every 30s, `gps` every 40s (needs fix)
- **Backend:** `IndietroTuttaBackend` on Render `https://indietrotutta.onrender.com`
  - DB: Turso `libsql://indietrotutta-rouggio.aws-eu-west-1.turso.io` via `TURSO_DATABASE_URL`/`TURSO_AUTH_TOKEN` in `IndietroTuttaBackend/.env` (gitignored) + Render env
  - Tables live: `devices`, `gps_points` (500 cap), `courses` (new)
  - OTA: `public/ota/latest.txt` + `firmware.bin`, `make dist` triggers `REDPLOY_HOOK_URL` from `Backend/.env`
- **Frontend:** Leaflet map, device list `live(<90s)/idle(<10m)/offline`, calendar `Live ↔ ?date`, `GET /gps?date=&deviceId=`
- **Docs:** `FEATURES.md` (full roadmap), `FEATURES_courses.svg` (5 templates), `PLAN.md` (this file)

## Git Branches — Incremental Steps (holds tight)

**Strategy:** Each step is a commit/branch that alone passes `make compile` + `GET /health` + `GET /gps` + `GET /devices`. No step breaks `speed` or heartbeat.

- `main` (device `bfdb500`, backend `b007daf` + `main` `b358c5c`) — last stable
- `step-0/templates` (backend) — **DONE**: `courses/templates.json` (W/L, W/L Gate, Triangle, WLT Olympic, Trapezoid) + `GET /courses/templates`, empty `services/` removed
  - Test: `curl /courses/templates` → 5
- `step-0/templates` + 1 commit `ddd2a2d` — **DONE Step 1**: `courses` Turso table + `store/courseStore.js` + CRUD `POST/GET/PUT/DELETE /courses`
  - Test: `POST /courses` → `GET /courses/:id` → `PUT` bumps `version` → `DELETE`

**Next:** continue on same branch `step-0/templates` (it now contains Step 0+1) or cut `step-1/*` from it. Current HEAD is `ddd2a2d`.

## How to Continue (new session)
1. Read `FEATURES.md` (§10 Incremental Plan Steps 0-8) + `PLAN.md` + `FEATURES_courses.svg`
2. Checkout branch: `git -C IndietroTuttaBackend checkout step-0/templates && git status`
3. Verify env: `IndietroTuttaBackend/.env` must have `TURSO_DATABASE_URL`, `TURSO_AUTH_TOKEN`, `REDPLOY_HOOK_URL` (not committed, use `.env.example` if missing)
4. Local test: `cd IndietroTuttaBackend && node server.js` → `curl localhost:3000/health`, `/devices`, `/courses/templates`, `/courses`
5. Device test: `cd IndietroTutta && make compile` (must be SUCCESS)
6. Pick next step from below, implement, `make compile` + local curl, commit, push branch

## Next Increments (from FEATURES.md §10)

**Step 2 — Course Cache + Wireframe Stub + POS** (device foundation, no backend)
- Device: `course` struct, NVS cache `courseVersion`, `getSyncedTime()` (GPS > server+millis), `drawWireframe()` 40×40, main `POS` line
- Test: hardcode W/L, see thumbnail + POS update

**Step 3 — Countdown on Main (GPS-synced)**
- Backend: `races` table, `POST /races` with `startTime`; Device: `health` returns `startTime`, `remaining = startTime - gpsTime`
- Test: set `now+2:00`, two devices count down delta <1s

**Step 4 — Next Mark + Pass**
- Device: distance/relative bearing, `PASSED` via radius + side `P/S/G`
- Test: sail 2-mark stick, `342m → 0m ✓` auto-advance

**Step 5 — Training Sail-to-Build + Start Line**
- Device: `TRAINING` screen, start-line fallback (`course` vs `p1→p2`), line-cross detection
- Test: flag while drifting vs moving both valid

**Step 6 — Web Builder with Templates → Push**
- Frontend: Leaflet draggable marks, `P/S/G` toggle, `POST /courses`, attach to race
- Test: draw W/L Gate on web, device wireframe matches within 5m

**Step 7 — Signals + Alerts**
- Backend `signals` table, device polls 5s when live, banner `RECALL` 5s

**Step 8+ — Auth, Rankings, Replay**

Gate for each: `make compile` SUCCESS, `make dist` → Render live → `health`+`gps` OK → update this file.

## Quick Commands
```sh
# backend local
cd IndietroTuttaBackend && node server.js
curl http://localhost:3000/courses/templates
curl http://localhost:3000/courses -X POST -H "Content-Type: application/json" -d '{"name":"Test","marks":[{"latOffset":0.009,"lonOffset":0,"radius":30}]}'

# device
cd IndietroTutta && make compile

# backend deploy (via device repo hook or direct push)
cd IndietroTutta && make dist  # bumps BUILD_VERSION, pushes firmware, triggers Render hook, polls latest.txt
# or: cd IndietroTuttaBackend && git push && curl -X POST "$REDPLOY_HOOK_URL"

# check live
curl https://indietrotutta.onrender.com/devices
curl https://indietrotutta.onrender.com/courses/templates
```

## Open Questions / Decisions Log
- Templates: 5 standard shapes, same JSON for web + device (decided)
- Start line: one-flag perpendicular with `p1→p2` fallback (decided, see FEATURES.md §5)
- Main screen: speed always, countdown + POS + next mark asides (decided)
- Fleet projection: not on device (decided)

Update this file after each step.

# IndietroTutta — hardware & bring-up notes

The **physical** reference: boards, wiring, flashing, what to expect on each USB
port. Firmware behaviour (screens, UI, race engine, OTA flow, backend calls,
gates) lives in `AGENTS.md` — that file is the source of truth, this one is not
allowed to contradict it.

Rewritten 2026-11-01. The old version was generated from source in 2026-08 and
had drifted badly (WAYPOINTS/TIMERS screens, `screen_serial.cpp`, flagged
waypoints, a four-page cycle — all of them long gone). If something here looks
older than the code, the code wins.

## Two boards, two buildchains, one `src/`

| | v1 — in the field | v2 — the new build |
|---|---|---|
| PlatformIO env | `esp32dev` (default) | `esp32s3` |
| MCU | classic ESP32 | ESP32-S3 N16R8 (QFN56 v0.2) |
| Flash | 4 MB | 16 MB QIO, `default_16MB.csv` (dual-OTA) |
| PSRAM | none | 8 MB **OPI** |
| USB | CP210x/CH340 bridge | native USB-Serial/JTAG **and** a CH343 bridge (dual Type-C) |
| Display | ST7789 240×320 | same setup for now — the 3.5" 320×480 ST7796S is not wired yet |
| OTA channel | `/ota` | `/ota/v2` |
| MAC seen by the backend | `08:D1:F9:C8:4D:24` (Ciccio) | `14:C1:9F:CD:6D:FC` (Scarolina) |

Select with `PIOENV`: `make compile PIOENV=esp32s3`, `make upload PORT=COM3
PIOENV=esp32s3`. The Makefile derives the firmware path **and** the OTA channel
from it, so publishing v2 is `make dist PIOENV=esp32s3`.

## Wiring

`include/board_pins.h` is the firmware's pin map (per variant, selected by
`-DDEVICE_VARIANT_V2`); `include/User_Setup.h` is the display's (force-included).
Do not hardcode a pin in a module.

| Signal | v1 | v2 | why it differs |
|---|---|---|---|
| Button L | 21 | 21 | |
| Button R | 22 | **38** | 22 does not exist on an S3 |
| Button 3 | — | **39** | v2 adds a front button; **no UI action bound yet** (40/41 free) |
| Buzzer | 19 | **47** | 19/20 are the S3's native USB pads |
| GPS RX / TX | 16 / 17 | 16 / 17 | UART2 @ 9600 |
| TFT CS / DC / RST | 5 / 2 / 4 | 5 / 2 / 4 | unchanged, all valid on S3 |
| TFT MOSI / SCLK | 23 / 18 | 23 / 18 | v1 panel config still in use |

Buttons are active-low with pullups, 50 ms debounce, 500 ms long-press.

## ESP32-S3 traps (why the v2 map looks like this)

- **GPIO 22–25 do not exist** (S3 = 0–21, 26–48).
- **GPIO 19/20 are the native USB D−/D+ pads.** The v1 boot chime on 19 grabbed
  them: the USB console and the OTA link died while the app kept running and the
  portal still answered — which reads exactly like a dead board.
- **GPIO 26–37 are the QSPI flash + OPI PSRAM bus**, 0/3/45/46 are strapping,
  43/44 are UART0.
- **`-DUSE_FSPI_PORT` is load-bearing.** Without it the app boot-panics in a loop
  (`StoreProhibited, EXCVADDR 0x000010`, `begin_tft_write` ← `TFT_eSPI::init` ←
  `screenInit`): TFT_eSPI sets `SPI_PORT = FSPI = 1` and IDF's
  `REG_SPI_BASE(i)` is `0` for `i < 2`, so the first `writecommand()` stores to
  `0x10`. Forcing SPI_PORT to 2 selects GPSPI2, the bus `SPI` really uses. IDF
  version independent, nothing to do with core 2.x vs 3.x.

## Flashing and console

| Port | VID:PID | Shows | Use it for |
|---|---|---|---|
| native USB-Serial/JTAG | `303A:1001` | the app's `Serial` (`ARDUINO_USB_CDC_ON_BOOT=1`), and flashing | normal dev, `/serial` equivalent |
| CH343 bridge | `1A86:55D3` | ROM banner + **panics only** (UART0) | bring-up debugging: it survives a crash-loop, the native port does not |

A device that "disappears" from USB while the access point still works is very
likely driving 19/20 — check the pin map before reflashing anything.

## Identity the backend sees

- `DeviceId` = the **STA MAC**, so a v2 board is a *different boat* in `/devices`.
- The open AP is **`IndietroTutta-XXXX`** (last two MAC bytes, e.g.
  `IndietroTutta-6DFC`) — two unsuffixed boards in range were indistinguishable
  and the portal could drive the wrong one. `/status` reports it as `sys.ap`.
- Addresses are DHCP and roam between networks; read `/devices` for the current
  LAN `ip` rather than guessing one.

## Panel status (v2)

v2 boots, joins WiFi, serves the portal and updates over the air, but **paints
nothing meaningful**: the layout hardcodes the 320×240 frame (`screen_race.cpp`:
`MAP_W = 320`, `CELL_EDGE {0,107,213,320}`), so the 320×480 ST7796S needs its own
port before the enclosure closes. Until then v2 keeps the v1 ST7789 setup, which
is why a bare board shows a 240×320 picture on nothing.

## Read next

- `AGENTS.md` — firmware architecture, UI navigation, race engine, OTA, portal,
  gates. The authority on behaviour.
- `SPEC-mark-passing.md` — LOCKED mark-passing spec (read before touching pass
  or score logic).
- `../PLAN.md` (session history) · `../ROADMAP.md` (features, canonical)
- `../IndietroTuttaEnclosure/SPECS.md` — mechanical/electrical spec for the box.

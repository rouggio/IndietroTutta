# IndietroTutta — Local build & upload

Quick commands to automate build, upload and OTA publishing locally.
Works on Linux, macOS and Windows.

## Prerequisites

- **Python 3** — creates the project virtualenv (`.venv`) that hosts PlatformIO
- **Git** — on Windows, Git for Windows also provides the bash/sed/grep tools the Makefile relies on
- **GNU Make**
  - Linux/macOS: usually preinstalled
  - Windows: `winget install ezwinports.make`

## Setup virtualenv and install PlatformIO

```bash
make install
```

## Build

```bash
make build                 # v1 (esp32dev), the default
make build PIOENV=esp32s3  # v2 (ESP32-S3)
```

Alias of `make compile`; firmware output lands in
`.pio/build/<PIOENV>/firmware.bin` (`esp32dev` / `esp32s3`). Every target takes
`PIOENV`, so the same commands build and flash either board — see
`DEVICE_NOTES.md` for the variants and their pin maps.

## Upload (flash over USB)

```bash
make upload
```

The serial port is auto-detected when omitted. To force a specific port:

```bash
make upload PORT=/dev/ttyACM0   # Linux/macOS
make upload PORT=COM5           # Windows
```

## Monitor serial

```bash
make monitor
```

Accepts the same `PORT=` override as `upload` (plus `BAUD=`, default 115200).

## Deploy (build + flash)

```bash
make deploy
```

## Publish an OTA release

```bash
make dist                   # v1 -> public/ota/
make dist PIOENV=esp32s3    # v2 -> public/ota/v2/
```

Increments `BUILD_VERSION` in `src/config.h`, rebuilds, copies `firmware.bin`
and `latest.txt` into the backend repo — `public/ota/` for v1, `public/ota/v2/`
for v2 — commits/pushes them, fires the Render hook and polls until the server
serves the new version. Requires your git identity to be configured and push
access to the backend repository.

**The channel follows `PIOENV`**: publishing without it puts a v1 image on the
v1 channel, which is what v1 boards pull. To stage a build locally without
touching the cloud use `make ota-local [PIOENV=…]`, or `make dl` to also make
the device pull it immediately.

## Clean

```bash
make clean
```

## Auto watch (Linux/macOS only)

Requires `entr`:

```bash
make watch
```

Alternative, requires `inotifywait`:

```bash
./scripts/watch.sh
```

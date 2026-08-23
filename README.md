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
make build
```

Alias of `make compile`; firmware output lands in `.pio/build/esp32dev/firmware.bin`.

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
make dist
```

Increments `BUILD_VERSION` in `src/config.h`, rebuilds, copies `firmware.bin`
and `latest.txt` into the backend repo (`public/ota/`) and commits/pushes them,
so devices update over the air. Requires your git identity to be configured and
push access to the backend repository.

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

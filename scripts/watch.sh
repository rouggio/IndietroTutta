#!/usr/bin/env bash
set -euo pipefail

# Simple watch loop using inotifywait. Requires inotify-tools (inotifywait).
# On change it runs build+upload via the Makefile.

PORT=${PORT:-/dev/ttyUSB0}

while inotifywait -e modify -r src include platformio.ini >/dev/null 2>&1; do
  echo "Change detected — building and uploading..."
  make build && make upload
done

"""Framebuffer grabber: GET /screen (raw RGB565 BE) -> PNG. Stdlib only.

Usage:  python scripts/grab_screen.py [host] [out.png]
Default host 192.168.0.106 (Ciccio, DHCP — confirm via GET /status first).
Takes seconds (SPI reads on the device); a debugging tool, not a live feed.

Grabs are kept in ../grab (project root): a bare filename lands there, and with
no name at all the file becomes <host>_<page>_<timestamp>.png.
"""
import json
import os
import struct
import sys
import urllib.request
import zlib
from datetime import datetime

HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.106"
DEVICE_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT_ROOT = os.path.dirname(DEVICE_REPO)  # umbrella: PLAN.md / ROADMAP.md live here
GRAB_DIR = os.path.join(
    PROJECT_ROOT
    if os.path.isfile(os.path.join(PROJECT_ROOT, "PLAN.md"))
    else DEVICE_REPO,  # device repo cloned on its own: keep the grabs local
    "grab",
)
OUT = sys.argv[2] if len(sys.argv) > 2 else ""
if OUT and not os.path.dirname(OUT):
    OUT = os.path.join(GRAB_DIR, OUT)  # a bare filename always lands in grab/
if not OUT:
    page = "screen"
    try:  # name the file after what is on the panel; a dead portal is not fatal
        with urllib.request.urlopen(f"http://{HOST}/status", timeout=5) as r:
            page = json.load(r).get("sys", {}).get("page", "screen")
    except Exception:
        pass
    OUT = os.path.join(
        GRAB_DIR,
        f"{HOST.replace(':', '-')}_{page}_{datetime.now():%Y%m%d-%H%M%S}.png",
    )
os.makedirs(os.path.dirname(OUT) or GRAB_DIR, exist_ok=True)


def panel_rows():
    """Width x height of the framebuffer, from /status when possible.

    The capture is a full frame (w*h*2 bytes), so the size follows the panel:
    it was 320x240 on the ST7789 and is 480x320 on the 3.5" ST7796S. /status
    reports the band height and band count, which multiply to the row count;
    the width is then len/2/rows. Falls back to the last known panel.
    """
    try:
        with urllib.request.urlopen(f"http://{HOST}/status", timeout=5) as r:
            scr = json.load(r).get("screen", {})
        rows = int(scr.get("bandH", 0)) * int(scr.get("bands", 0))
        if rows > 0:
            return rows
    except Exception:
        pass
    return 0


ROWS = panel_rows()
W, H = 480, 320  # default: the ST7796S at rotation 1


def chunk(ctype, data):
    return struct.pack(">I", len(data)) + ctype + data + struct.pack(
        ">I", zlib.crc32(ctype + data) & 0xFFFFFFFF
    )


with urllib.request.urlopen(f"http://{HOST}/screen", timeout=180) as r:
    data = r.read()

want = W * H * 2
if ROWS:
    # Derive both dimensions from what the device actually sent: the frame is
    # RGB565, so bytes = w * rows * 2.
    H = ROWS
    if len(data) % (2 * H) == 0:
        W = len(data) // (2 * H)
        want = len(data)
if len(data) != want:
    sys.exit(f"expected {want} bytes, got {len(data)}")

raw = bytearray(want * 3 // 2)
o = 0
for i in range(0, want, 2):
    px = (data[i] << 8) | data[i + 1]
    raw[o] = ((px >> 11) & 31) * 255 // 31
    raw[o + 1] = ((px >> 5) & 63) * 255 // 63
    raw[o + 2] = (px & 31) * 255 // 31
    o += 3

stride = W * 3
scan = b"".join(b"\x00" + bytes(raw[y * stride:(y + 1) * stride]) for y in range(H))
png = (
    b"\x89PNG\r\n\x1a\n"
    + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    + chunk(b"IDAT", zlib.compress(scan))
    + chunk(b"IEND", b"")
)
with open(OUT, "wb") as f:
    f.write(png)
print(f"wrote {OUT} ({W}x{H})")

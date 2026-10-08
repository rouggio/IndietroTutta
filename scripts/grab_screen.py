"""Framebuffer grabber: GET /screen (raw RGB565 BE) -> PNG. Stdlib only.

Usage:  python scripts/grab_screen.py [host] [out.png]
Default host 192.168.0.106 (Ciccio, DHCP — confirm via GET /status first).
Takes seconds (SPI reads on the device); a debugging tool, not a live feed.
"""
import struct
import sys
import urllib.request
import zlib

HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.106"
OUT = sys.argv[2] if len(sys.argv) > 2 else "screen.png"
W, H = 320, 240  # rotation-3 landscape framebuffer


def chunk(ctype, data):
    return struct.pack(">I", len(data)) + ctype + data + struct.pack(
        ">I", zlib.crc32(ctype + data) & 0xFFFFFFFF
    )


with urllib.request.urlopen(f"http://{HOST}/screen", timeout=180) as r:
    data = r.read()

want = W * H * 2
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

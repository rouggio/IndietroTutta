"""Convert assets/logo.png to a native RGB565 C array for the splash screen.

Writes include/splash_logo.h (PROGMEM, readable straight from flash).
Run from the IndietroTutta/ directory after (re)generating assets/logo.png:
    python assets/make_splash_header.py
"""
from PIL import Image

WANT = (67, 67)

img = Image.open("assets/logo.png").convert("RGB")
assert img.size == WANT, f"logo.png must be {WANT}, is {img.size}"
px = img.load()

words = []
for y in range(WANT[1]):
    for x in range(WANT[0]):
        r, g, b = px[x, y]
        native = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        # pushImage writes words high-byte-first on the wire, so pre-swap here
        words.append(((native >> 8) & 0xFF) | ((native << 8) & 0xFF00))

with open("include/splash_logo.h", "w") as f:
    f.write("#pragma once\n")
    f.write("#include <Arduino.h>\n\n")
    f.write("// Generated from assets/logo.png - do not edit by hand.\n")
    f.write(f"#define SPLASH_LOGO_W {WANT[0]}\n")
    f.write(f"#define SPLASH_LOGO_H {WANT[1]}\n\n")
    f.write("const uint16_t splashLogo[] PROGMEM = {\n")
    for i in range(0, len(words), 8):
        f.write("  " + ", ".join(f"0x{w:04X}" for w in words[i:i + 8]) + ",\n")
    f.write("};\n")
print(f"wrote include/splash_logo.h ({len(words)} px)")

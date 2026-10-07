"""Regenerate assets/logo.png from the frontend favicon geometry.

Frontend logo: navy #0f172a square, white sail, red #e41a1c hull
(public/favicon.svg, 64x64 viewBox). Rendered here at 180x180 so it can be
pushed straight to the ST7789 splash screen (see make_splash_header.py).
"""
from PIL import Image, ImageDraw

SIZE = 67
NAVY = (15, 23, 42)
WHITE = (255, 255, 255)
RED = (228, 26, 28)

S = SIZE / 64.0


def scale(pts):
    return [(x * S, y * S) for x, y in pts]


SAIL = [(32, 8), (50, 46), (32, 39), (14, 46)]
HULL = [(14, 50), (50, 50), (41, 57), (23, 57)]

img = Image.new("RGB", (SIZE, SIZE), NAVY)
d = ImageDraw.Draw(img)
d.polygon(scale(SAIL), fill=WHITE)
d.polygon(scale(HULL), fill=RED)
img.save("assets/logo.png")
print("wrote assets/logo.png", img.size)

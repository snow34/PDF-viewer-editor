#!/usr/bin/env python3
"""Draws the app icon: overprinting cyan, magenta and yellow discs on paper.
Writes resources/app.png and packaging/windows/app.ico. Needs Pillow."""
from PIL import Image, ImageChops, ImageDraw

S = 1024


def disc(color, cx, cy, r):
    im = Image.new("RGB", (S, S), (255, 255, 255))
    ImageDraw.Draw(im).ellipse((cx - r, cy - r, cx + r, cy + r), fill=color)
    return im


inks = Image.new("RGB", (S, S), (255, 255, 255))
for color, (cx, cy) in (((0, 174, 239), (512, 400)), ((236, 0, 140), (400, 610)), ((255, 242, 0), (624, 610))):
    inks = ImageChops.multiply(inks, disc(color, cx, cy, 250))  # multiply = overprint

mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle((40, 40, S - 40, S - 40), radius=180, fill=255)
out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
out.paste(inks, (0, 0), mask)
ImageDraw.Draw(out).rounded_rectangle((40, 40, S - 40, S - 40), radius=180, outline=(60, 60, 60, 255), width=28)

icon = out.resize((256, 256), Image.LANCZOS)
icon.save("resources/app.png")
icon.save("packaging/windows/app.ico",
          sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

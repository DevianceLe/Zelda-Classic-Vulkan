"""Converts upstream's ZC logo PNGs into the small file the 3.0 Nightly intro loads.

usage: make_intro_logo.py <folder with ZC_Logo*.png> <out.dat>

The PNGs are pixel art blown up about 9.5 times, so each is sampled back to its true size.
Format, little-endian: "ZC3L", u16 width, u16 height, u16 colours, then colours x RGB (8 bit),
then three width x height images (whole logo, logo without the sword, the sword alone).
Pixel value 0 is see-through; value n is colour n - 1.
"""
import struct
import sys

from PIL import Image

W, H = 249, 91
NAMES = ("ZC_Logo.png", "ZC_Logo_NoSword.png", "ZC_Logo_Sword.png")


def main(folder, out):
    images = []

    for name in NAMES:
        img = Image.open(f"{folder}/{name}").convert("RGBA")
        images.append(img.resize((W, H), Image.NEAREST))

    colours = {}
    layers = []

    for img in images:
        layer = bytearray()

        for r, g, b, a in img.getdata():
            if a < 128:
                layer.append(0)
                continue

            index = colours.setdefault((r, g, b), len(colours))

            if index > 240:
                raise SystemExit(f"too many colours in {len(colours)}")

            layer.append(index + 1)

        layers.append(layer)

    with open(out, "wb") as f:
        f.write(b"ZC3L" + struct.pack("<HHH", W, H, len(colours)))

        for colour in sorted(colours, key=colours.get):
            f.write(bytes(colour))

        for layer in layers:
            f.write(layer)

    print(out, W, H, "colours", len(colours))


if __name__ == "__main__":
    main(*sys.argv[1:3])

#!/usr/bin/env python3
"""Generates the embedded assets of TWiLight Hub.

- assets/font_*.bin: DejaVu Sans rasterised at several sizes into a compact
  4bpp anti-aliased "HFN4" format (Latin, Greek, Cyrillic and punctuation).
- assets/icons.bin: anti-aliased 8-bit alpha masks drawn with Pillow, tinted
  at runtime (so the same icon works in the dark and the light theme).
- icon.bmp: 32x32 16-colour banner icon.

Requires Pillow and fontTools. Run from the repository root:
    python3 hub/tools/make_assets.py
"""

import math
import os
import struct
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ROOT)
OUT = os.path.join(ROOT, "assets")

# --------------------------------------------------------------------------
# Fonts: DejaVu Sans rasterised with FreeType (via Pillow) into 4bpp
# anti-aliased glyphs. The Bitstream Vera/DejaVu license allows embedding;
# its notice is in assets/DejaVu-LICENSE.txt.
# --------------------------------------------------------------------------

FONT_SEARCH = [
    "/usr/share/fonts/truetype/dejavu",
    "/usr/share/fonts/TTF",
    "/usr/share/fonts/dejavu",
    os.path.join(ROOT, "tools", "fonts"),
]

RANGES_TEXT = [
    (0x0020, 0x007E), (0x00A0, 0x024F), (0x0370, 0x03FF), (0x0400, 0x04FF),
    (0x2010, 0x2027), (0x2030, 0x203A), (0x20AC, 0x20AC), (0x2122, 0x2122),
    (0x2190, 0x2193), (0x2713, 0x2713), (0xFFFD, 0xFFFD),
]
RANGES_LATIN = [(0x0020, 0x007E), (0x00A0, 0x00FF), (0x2013, 0x2014), (0x2026, 0x2026), (0xFFFD, 0xFFFD)]

# name, file, pixel size, ranges
FONTS = [
    ("body", "DejaVuSans.ttf", 12, RANGES_TEXT),
    ("bold", "DejaVuSans-Bold.ttf", 12, RANGES_TEXT),
    ("title", "DejaVuSans-Bold.ttf", 16, RANGES_LATIN),
    ("head", "DejaVuSans-Bold.ttf", 20, RANGES_LATIN),
    ("huge", "DejaVuSans-Bold.ttf", 36, RANGES_LATIN),
]


def find_font(name):
    for d in FONT_SEARCH:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None


def write_font(path, size, ranges, dst):
    from PIL import ImageFont
    from fontTools.ttLib import TTFont

    cmap = TTFont(path).getBestCmap()
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    cps = sorted(c for lo, hi in ranges for c in range(lo, hi + 1) if c in cmap)
    pad = size
    glyphs, data = [], bytearray()
    for c in cps:
        ch = chr(c)
        im = Image.new("L", (size * 3 + pad * 2, size * 3 + pad * 2), 0)
        ImageDraw.Draw(im).text((pad, pad + ascent), ch, font=font, fill=255, anchor="ls")
        bbox = im.getbbox()
        adv = int(round(font.getlength(ch)))
        if bbox is None:
            glyphs.append((0, 0, 0, 0, adv, len(data)))
            continue
        x0, y0, x1, y1 = bbox
        g = im.crop(bbox)
        w, h = g.size
        px = g.tobytes()
        off = len(data)
        for y in range(h):
            row = px[y * w:(y + 1) * w]
            for x in range(0, w, 2):
                hi = row[x] >> 4
                lo = row[x + 1] >> 4 if x + 1 < w else 0
                data.append((hi << 4) | lo)
        glyphs.append((x0 - pad, y0 - pad, w, h, adv, off))
    out = bytearray(b"HFN4")
    out += struct.pack("<BBH", ascent + descent, ascent, len(cps))
    for c in cps:
        out += struct.pack("<H", c)
    while len(out) % 4:
        out += b"\0"
    for xo, yo, w, h, adv, off in glyphs:
        out += struct.pack("<bbBBBBHI", xo, yo, w, h, adv, 0, 0, off)
    out += data
    open(dst, "wb").write(out)
    print(f"{os.path.basename(dst)}: {len(cps)} glyphs, {len(out)} bytes")


# --------------------------------------------------------------------------
# Icons (drawn on a 256x256 canvas, white = opaque)
# --------------------------------------------------------------------------

S = 256
W, K = 255, 0  # white / black (cut out)


def canvas():
    im = Image.new("L", (S, S), 0)
    return im, ImageDraw.Draw(im)


def ring(d, cx, cy, r, width, fill=W):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=fill)
    r2 = r - width
    d.ellipse((cx - r2, cy - r2, cx + r2, cy + r2), fill=K if fill == W else W)


def disc(d, cx, cy, r, fill=W):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=fill)


def line(d, pts, width, fill=W):
    d.line(pts, fill=fill, width=width)
    for p in pts:
        disc(d, p[0], p[1], width / 2, fill)


def arc(d, cx, cy, r, width, a0, a1, fill=W, caps=True):
    d.arc((cx - r, cy - r, cx + r, cy + r), a0, a1, fill=fill, width=width)
    if caps:
        rm = r - width / 2
        for a in (a0, a1):
            disc(d, cx + rm * math.cos(math.radians(a)), cy + rm * math.sin(math.radians(a)), width / 2, fill)


def rays(d, cx, cy, r0, r1, n, width, phase=0.0):
    for i in range(n):
        a = math.radians(phase + i * 360 / n)
        line(d, [(cx + r0 * math.cos(a), cy + r0 * math.sin(a)), (cx + r1 * math.cos(a), cy + r1 * math.sin(a))], width)


def icon_camera():
    im, d = canvas()
    d.rounded_rectangle((20, 70, 236, 214), 30, fill=W)
    d.rounded_rectangle((82, 44, 174, 90), 16, fill=W)
    disc(d, 128, 142, 60, K)
    disc(d, 128, 142, 46, W)
    disc(d, 128, 142, 26, K)
    disc(d, 200, 100, 11, K)
    return im


def icon_album():
    im, d = canvas()
    d.rounded_rectangle((22, 40, 234, 216), 26, fill=W)
    d.rounded_rectangle((42, 60, 214, 196), 12, fill=K)
    d.polygon([(52, 190), (104, 118), (142, 166), (168, 138), (206, 190)], fill=W)
    disc(d, 172, 94, 18)
    return im


def icon_globe():
    im, d = canvas()
    d.ellipse((74, 22, 182, 234), outline=W, width=14)
    line(d, [(128, 24), (128, 232)], 14)
    line(d, [(24, 128), (232, 128)], 14)
    line(d, [(46, 76), (210, 76)], 12)
    line(d, [(46, 180), (210, 180)], 12)
    # Clip everything to the globe, then draw its outline
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).ellipse((20, 20, 236, 236), fill=W)
    im = Image.composite(im, Image.new("L", (S, S), 0), mask)
    d = ImageDraw.Draw(im)
    d.ellipse((20, 20, 236, 236), outline=W, width=18)
    return im


def icon_wifi():
    im, d = canvas()
    cx, cy = 128, 206
    for r in (184, 128, 72):
        arc(d, cx, cy, r, 30, 225, 315)
    disc(d, cx, cy - 6, 24)
    return im


def icon_weather():
    im, d = canvas()
    disc(d, 92, 96, 46)
    rays(d, 92, 96, 62, 86, 8, 16, 22.5)
    # Cloud cut-out then cloud
    for grow, col in ((14, K), (0, W)):
        disc(d, 104, 168, 42 + grow, col)
        disc(d, 156, 146, 56 + grow, col)
        disc(d, 204, 174, 36 + grow, col)
        d.rounded_rectangle((62 - grow, 168 - grow, 238 + grow, 214 + grow), 22 + grow, fill=col)
    return im


def icon_clock():
    im, d = canvas()
    ring(d, 128, 128, 108, 18)
    for i in range(12):
        a = math.radians(i * 30)
        r0 = 76 if i % 3 else 70
        line(d, [(128 + r0 * math.cos(a), 128 + r0 * math.sin(a)), (128 + 82 * math.cos(a), 128 + 82 * math.sin(a))], 8)
    line(d, [(128, 128), (128, 60)], 16)
    line(d, [(128, 128), (178, 152)], 16)
    disc(d, 128, 128, 16)
    return im


def icon_boxart():
    im, d = canvas()
    d.rounded_rectangle((40, 22, 216, 234), 18, fill=W)
    d.rounded_rectangle((58, 40, 198, 216), 8, fill=K)
    d.rectangle((58, 40, 198, 66), fill=W)
    line(d, [(128, 90), (128, 164)], 22)
    d.polygon([(84, 146), (172, 146), (128, 196)], fill=W)
    return im


def icon_info():
    im, d = canvas()
    disc(d, 128, 128, 112)
    disc(d, 128, 74, 17, K)
    d.rounded_rectangle((112, 106, 144, 196), 10, fill=K)
    return im


def icon_power():
    im, d = canvas()
    arc(d, 128, 140, 96, 24, -52, 232)
    line(d, [(128, 26), (128, 118)], 24)
    return im


def icon_gear():
    im, d = canvas()
    disc(d, 128, 128, 80)
    for i in range(8):
        a = i * 45
        tooth = Image.new("L", (S, S), 0)
        td = ImageDraw.Draw(tooth)
        td.rounded_rectangle((108, 18, 148, 70), 8, fill=W)
        tooth = tooth.rotate(a, resample=Image.BICUBIC, center=(128, 128))
        im.paste(W, (0, 0), tooth)
    d = ImageDraw.Draw(im)
    disc(d, 128, 128, 36, K)
    return im


def icon_moon():
    im, d = canvas()
    disc(d, 128, 128, 100)
    disc(d, 176, 92, 88, K)
    return im


def icon_sun():
    im, d = canvas()
    disc(d, 128, 128, 52)
    rays(d, 128, 128, 76, 108, 8, 20)
    return im


def icon_back():
    im, d = canvas()
    line(d, [(64, 128), (204, 128)], 26)
    line(d, [(124, 64), (60, 128), (124, 192)], 26)
    return im


def icon_forward():
    return icon_back().transpose(Image.FLIP_LEFT_RIGHT)


def icon_lock(open_=False):
    im, d = canvas()
    if open_:
        arc(d, 172, 104, 54, 22, 180, 360)
        line(d, [(118 + 11, 104), (118 + 11, 118)], 22)
    else:
        arc(d, 128, 104, 56, 22, 180, 360)
        line(d, [(83, 104), (83, 124)], 22)
        line(d, [(173, 104), (173, 124)], 22)
    d.rounded_rectangle((52, 112, 204, 226), 20, fill=W)
    disc(d, 128, 158, 16, K)
    d.rectangle((121, 158, 135, 196), fill=K)
    return im


def icon_star():
    im, d = canvas()
    pts = []
    for i in range(10):
        a = math.radians(-90 + i * 36)
        r = 112 if i % 2 == 0 else 48
        pts.append((128 + r * math.cos(a), 136 + r * math.sin(a)))
    d.polygon(pts, fill=W)
    return im


def icon_search():
    im, d = canvas()
    ring(d, 108, 108, 72, 22)
    line(d, [(162, 162), (218, 218)], 32)
    return im


def icon_refresh():
    im, d = canvas()
    arc(d, 128, 128, 92, 24, 40, 320, caps=False)
    a = math.radians(40)
    cx, cy = 128 + 80 * math.cos(a), 128 + 80 * math.sin(a)
    d.polygon([(cx - 36, cy - 8), (cx + 38, cy - 20), (cx + 10, cy + 50)], fill=W)
    return im


def icon_home():
    im, d = canvas()
    d.polygon([(128, 30), (236, 124), (212, 124), (212, 222), (44, 222), (44, 124), (20, 124)], fill=W)
    d.rounded_rectangle((104, 150, 152, 222), 8, fill=K)
    return im


def icon_trash():
    im, d = canvas()
    d.rounded_rectangle((40, 50, 216, 76), 8, fill=W)
    d.rounded_rectangle((96, 26, 160, 56), 10, fill=W)
    d.polygon([(58, 88), (198, 88), (184, 230), (72, 230)], fill=W)
    for x in (100, 128, 156):
        line(d, [(x, 110), (x, 206)], 12, K)
    return im


def icon_switchcam():
    im, d = canvas()
    d.rounded_rectangle((20, 70, 236, 214), 30, fill=W)
    d.rounded_rectangle((82, 44, 174, 90), 16, fill=W)
    arc(d, 128, 142, 44, 14, 200, 340, fill=K, caps=False)
    arc(d, 128, 142, 44, 14, 20, 160, fill=K, caps=False)
    d.polygon([(76, 128), (102, 128), (86, 154)], fill=K)
    d.polygon([(180, 156), (154, 156), (170, 130)], fill=K)
    return im


def icon_keyboard():
    im, d = canvas()
    d.rounded_rectangle((16, 60, 240, 196), 20, fill=W)
    for row, y in enumerate((84, 118)):
        for i in range(7):
            x = 38 + i * 28 + (row * 12)
            d.rounded_rectangle((x, y, x + 18, y + 18), 4, fill=K)
    d.rounded_rectangle((70, 152, 186, 170), 4, fill=K)
    return im


def icon_plus():
    im, d = canvas()
    line(d, [(128, 40), (128, 216)], 30)
    line(d, [(40, 128), (216, 128)], 30)
    return im


def icon_check():
    im, d = canvas()
    line(d, [(44, 132), (104, 190), (214, 70)], 30)
    return im


def icon_close():
    im, d = canvas()
    line(d, [(56, 56), (200, 200)], 30)
    line(d, [(200, 56), (56, 200)], 30)
    return im


def icon_shutter():
    im, d = canvas()
    ring(d, 128, 128, 112, 16)
    disc(d, 128, 128, 84)
    return im


def icon_tools():
    """Wrench, used for the "system/installation" shortcut."""
    im, d = canvas()
    line(d, [(70, 186), (150, 106)], 36)
    disc(d, 168, 88, 58)
    d.polygon([(168, 88), (240, 40), (240, 110)], fill=K)
    disc(d, 168, 88, 20, K)
    return im


def icon_download():
    im, d = canvas()
    line(d, [(128, 30), (128, 148)], 26)
    d.polygon([(70, 126), (186, 126), (128, 190)], fill=W)
    line(d, [(46, 220), (210, 220)], 22)
    return im


def cloud(d, dy=0, grow=0, col=W):
    disc(d, 88, 150 + dy, 48 + grow, col)
    disc(d, 146, 124 + dy, 62 + grow, col)
    disc(d, 196, 156 + dy, 40 + grow, col)
    d.rounded_rectangle((40 - grow, 150 + dy - grow, 236 + grow, 198 + dy + grow), 24 + grow, fill=col)


def icon_cloud():
    im, d = canvas()
    cloud(d, 10)
    return im


def icon_rain():
    im, d = canvas()
    cloud(d, -30)
    for x in (80, 128, 176):
        line(d, [(x + 10, 196), (x - 6, 236)], 16)
    return im


def icon_snow():
    im, d = canvas()
    cloud(d, -30)
    for x, y in ((84, 214), (128, 236), (172, 214)):
        disc(d, x, y, 12)
    return im


def icon_storm():
    im, d = canvas()
    cloud(d, -34)
    d.polygon([(136, 150), (96, 206), (126, 206), (108, 250), (166, 186), (134, 186), (152, 150)], fill=K)
    d.polygon([(132, 160), (104, 200), (128, 200), (114, 240), (158, 190), (132, 190), (146, 160)], fill=W)
    return im


def icon_fog():
    im, d = canvas()
    for i, y in enumerate((70, 116, 162, 208)):
        off = 20 if i % 2 else 0
        line(d, [(40 + off, y), (216 - 20 + off, y)], 22)
    return im


def icon_list():
    im, d = canvas()
    for y in (64, 128, 192):
        disc(d, 50, y, 16)
        line(d, [(92, y), (214, y)], 22)
    return im


ICONS = [
    ("camera", icon_camera), ("album", icon_album), ("globe", icon_globe),
    ("wifi", icon_wifi), ("weather", icon_weather), ("clock", icon_clock),
    ("boxart", icon_boxart), ("info", icon_info), ("power", icon_power),
    ("gear", icon_gear), ("moon", icon_moon), ("sun", icon_sun),
    ("back", icon_back), ("forward", icon_forward), ("lock", icon_lock),
    ("unlock", lambda: icon_lock(True)), ("star", icon_star),
    ("search", icon_search), ("refresh", icon_refresh), ("home", icon_home),
    ("trash", icon_trash), ("switchcam", icon_switchcam),
    ("keyboard", icon_keyboard), ("plus", icon_plus), ("check", icon_check),
    ("close", icon_close), ("shutter", icon_shutter), ("tools", icon_tools),
    ("download", icon_download), ("list", icon_list), ("cloud", icon_cloud),
    ("rain", icon_rain), ("snow", icon_snow), ("storm", icon_storm), ("fog", icon_fog),
]

SIZES = (64, 40, 24, 16)


def write_icons(dst, header):
    blob = bytearray(b"HICO")
    blob += struct.pack("<HH", len(ICONS), len(SIZES))
    for s in SIZES:
        blob += struct.pack("<H", s)
    while len(blob) % 4:
        blob += b"\0"
    for name, fn in ICONS:
        big = fn()
        for s in SIZES:
            # Supersampled rendering then a high quality downscale gives
            # smooth anti-aliased edges.
            blob += big.resize((s, s), Image.LANCZOS).tobytes()
    open(dst, "wb").write(blob)
    with open(header, "w") as h:
        h.write("// Generated by hub/tools/make_assets.py - do not edit\n#pragma once\n\n")
        h.write("enum IconId {\n")
        for name, _ in ICONS:
            h.write(f"\tICON_{name.upper()},\n")
        h.write("\tICON_COUNT\n};\n\n")
        h.write("enum IconSize {\n")
        for i, s in enumerate(SIZES):
            h.write(f"\tICONSIZE_{s} = {i},\n")
        h.write("};\n")
    print(f"icons.bin: {len(ICONS)} icons x {len(SIZES)} sizes, {len(blob)} bytes")


def write_banner_icon(dst):
    """32x32, 16 colours: a cyan rounded tile with a 2x2 grid of apps."""
    big = Image.new("RGB", (256, 256), (45, 45, 45))
    d = ImageDraw.Draw(big)
    d.rounded_rectangle((8, 8, 248, 248), 56, fill=(0, 190, 225))
    cols = [(255, 255, 255), (255, 255, 255), (255, 255, 255), (40, 40, 40)]
    for i, (x, y) in enumerate(((52, 52), (136, 52), (52, 136), (136, 136))):
        d.rounded_rectangle((x, y, x + 68, y + 68), 18, fill=cols[i] if i < 3 else (255, 255, 255))
    d.ellipse((150, 150, 190, 190), fill=(0, 190, 225))
    small = big.resize((32, 32), Image.LANCZOS).quantize(colors=16)
    small.save(dst)


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fname, size, ranges in FONTS:
        path = find_font(fname)
        if path is None:
            print(f"warning: {fname} not found, keeping the existing font_{name}.bin", file=sys.stderr)
            continue
        write_font(path, size, ranges, os.path.join(OUT, f"font_{name}.bin"))
    write_icons(os.path.join(OUT, "icons.bin"), os.path.join(ROOT, "arm9", "source", "icons.h"))
    write_banner_icon(os.path.join(ROOT, "icon.bmp"))
    if "--preview" in sys.argv:
        sheet = Image.new("L", (len(ICONS) * 68, 68), 40)
        for i, (name, fn) in enumerate(ICONS):
            sheet.paste(255, (i * 68 + 2, 2), fn().resize((64, 64), Image.LANCZOS))
        sheet.save(os.path.join(ROOT, "tools", "icons_preview.png"))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Generates the "Modern Dark" and "Modern Light" skins of the 3DS theme.

A flat look inspired by the current Nintendo system menus: dark or light grey
backgrounds, rounded tiles, a pulsing cyan selection frame, round shortcut
buttons and button hints.

Output: 7zfile/_nds/TWiLightMenu/3dsmenu/themes/Modern Dark|Light/
Requires Pillow. Run from anywhere:
    python3 "romsel_dsimenutheme/resources/modern theme/make_modern_theme.py" [--preview]

Notes on the formats used by the theme engine:
- Backgrounds can be PNG (16-bit, only fully opaque pixels are kept).
- Sprites (boxes, cursor, bubble tip, dialog box, settings icon) must be
  16-colour paletted images; they are written as 4-bit BMPs where palette
  index 0 is transparent. Anti-aliased edges are pre-blended with the colour
  the sprite is drawn over.
"""

import math
import os
import struct
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
OUT_BASE = os.path.join(REPO, "7zfile", "_nds", "TWiLightMenu", "3dsmenu", "themes")
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

SS = 4  # supersampling factor

VARIANTS = {
    "Modern Dark": dict(
        bg=(45, 45, 45), bar=(38, 38, 38), panel=(63, 63, 63), panel_hi=(74, 74, 74),
        border=(84, 84, 84), text=(255, 255, 255), dim=(180, 180, 180), accent=(31, 214, 240),
        empty=(52, 52, 52), shadow=None, glyph=(255, 255, 255), dark=True,
    ),
    "Modern Light": dict(
        bg=(235, 235, 235), bar=(245, 245, 245), panel=(255, 255, 255), panel_hi=(250, 250, 250),
        border=(205, 205, 205), text=(45, 45, 45), dim=(110, 110, 110), accent=(10, 180, 230),
        empty=(222, 222, 222), shadow=(0, 0, 0), glyph=(45, 45, 45), dark=False,
    ),
}


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def rgb15(c):
    r, g, b = (x * 31 // 255 for x in c)
    return 0x8000 | (b << 10) | (g << 5) | r


# ---------------------------------------------------------------------------
# Drawing helpers (supersampled)
# ---------------------------------------------------------------------------

class Canvas:
    def __init__(self, w, h, fill=(0, 0, 0, 0)):
        self.w, self.h = w, h
        self.im = Image.new("RGBA", (w * SS, h * SS), fill)
        self.d = ImageDraw.Draw(self.im)

    def rrect(self, box, r, fill=None, outline=None, width=1):
        x0, y0, x1, y1 = box
        self.d.rounded_rectangle((x0 * SS, y0 * SS, x1 * SS - 1, y1 * SS - 1), r * SS, fill=fill,
                                 outline=outline, width=width * SS)

    def circle(self, cx, cy, r, fill=None, outline=None, width=1):
        self.d.ellipse(((cx - r) * SS, (cy - r) * SS, (cx + r) * SS - 1, (cy + r) * SS - 1), fill=fill,
                       outline=outline, width=width * SS)

    def text(self, xy, s, size, fill, anchor="la"):
        f = ImageFont.truetype(FONT_BOLD, size * SS)
        self.d.text((xy[0] * SS, xy[1] * SS), s, font=f, fill=fill, anchor=anchor)

    def glyph(self, fn, box, color):
        """Draws a white-on-black mask glyph function scaled into box."""
        x0, y0, x1, y1 = box
        m = fn().resize(((x1 - x0) * SS, (y1 - y0) * SS), Image.LANCZOS)
        layer = Image.new("RGBA", m.size, color + (255,))
        self.im.paste(layer, (x0 * SS, y0 * SS), m)

    def result(self):
        return self.im.resize((self.w, self.h), Image.LANCZOS)


# Glyphs drawn on a 256x256 mask (white = opaque)
def g_canvas():
    im = Image.new("L", (256, 256), 0)
    return im, ImageDraw.Draw(im)


def g_gear():
    im, d = g_canvas()
    d.ellipse((48, 48, 208, 208), fill=255)
    for i in range(8):
        t = Image.new("L", (256, 256), 0)
        ImageDraw.Draw(t).rounded_rectangle((106, 14, 150, 70), 10, fill=255)
        im.paste(255, (0, 0), t.rotate(i * 45, resample=Image.BICUBIC, center=(128, 128)))
    d = ImageDraw.Draw(im)
    d.ellipse((92, 92, 164, 164), fill=0)
    return im


def g_home():
    im, d = g_canvas()
    d.polygon([(128, 26), (240, 124), (212, 124), (212, 226), (44, 226), (44, 124), (16, 124)], fill=255)
    d.rounded_rectangle((104, 152, 152, 226), 8, fill=0)
    return im


def g_folder():
    im, d = g_canvas()
    d.rounded_rectangle((20, 50, 120, 100), 16, fill=255)
    d.rounded_rectangle((20, 76, 236, 214), 18, fill=255)
    d.line((20, 96, 236, 96), fill=0, width=10)
    return im


# ---------------------------------------------------------------------------
# Writers
# ---------------------------------------------------------------------------

def save_png_opaque(img, path, bg):
    """Flattens img over bg (only fully opaque pixels survive in the engine)."""
    base = Image.new("RGBA", img.size, bg + (255,))
    base.alpha_composite(img)
    base.convert("RGB").save(path)


def save_png_keyed(img, path, bg):
    """Keeps transparency: pixels above half alpha are blended with bg and made
    opaque, the rest transparent."""
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    px, po = img.load(), out.load()
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            r, g, b, a = px[x, y]
            if a >= 96:
                t = a / 255
                po[x, y] = (int(r * t + bg[0] * (1 - t)), int(g * t + bg[1] * (1 - t)), int(b * t + bg[2] * (1 - t)), 255)
    out.save(path)


def save_bmp4(img, path, bg, alpha_cut=80):
    """16-colour BMP, index 0 transparent. img is RGBA; partially transparent
    pixels are blended with bg."""
    w, h = img.size
    assert w % 8 == 0, "width must be a multiple of 8"
    px = img.load()
    rgb = Image.new("RGB", (w, h))
    pr = rgb.load()
    mask = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            opaque = a >= alpha_cut
            mask.append(opaque)
            t = a / 255
            pr[x, y] = (int(r * t + bg[0] * (1 - t)), int(g * t + bg[1] * (1 - t)), int(b * t + bg[2] * (1 - t)))
    opaque_px = [pr[i % w, i // w] for i in range(w * h) if mask[i]]
    if opaque_px:
        sample = Image.new("RGB", (len(opaque_px), 1))
        sample.putdata(opaque_px)
        q = sample.quantize(colors=15, method=Image.Quantize.MEDIANCUT)
        pal = q.getpalette()[:15 * 3]
        qpx = list(q.get_flattened_data() if hasattr(q, "get_flattened_data") else q.getdata())
    else:
        pal, qpx = [0] * 45, []
    palette = [(255, 0, 255)] + [tuple(pal[i * 3:i * 3 + 3]) for i in range(len(pal) // 3)]
    while len(palette) < 16:
        palette.append((0, 0, 0))
    idx = []
    k = 0
    for i in range(w * h):
        if mask[i]:
            idx.append(qpx[k] + 1)
            k += 1
        else:
            idx.append(0)
    row_bytes = w // 2
    data = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(0, w, 2):
            data.append((idx[y * w + x] << 4) | idx[y * w + x + 1])
    header = struct.pack("<2sIHHI", b"BM", 14 + 40 + 64 + len(data), 0, 0, 14 + 40 + 64)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 4, 0, len(data), 2835, 2835, 16, 0)
    pal_bytes = b"".join(struct.pack("<BBBB", c[2], c[1], c[0], 0) for c in palette)
    with open(path, "wb") as f:
        f.write(header + info + pal_bytes + data)
    assert len(data) == row_bytes * h


# ---------------------------------------------------------------------------
# Theme pieces
# ---------------------------------------------------------------------------

def top_background(P):
    c = Canvas(256, 192, P["bg"] + (255,))
    # Subtle vertical gradient
    for y in range(192 * SS):
        t = y / (192 * SS)
        col = mix(mix(P["bg"], (255, 255, 255), 0.04 if P["dark"] else 0.5), P["bg"], t)
        c.d.line((0, y, 256 * SS, y), fill=col + (255,))
    c.d.rectangle((0, 0, 256 * SS, 21 * SS), fill=P["bar"] + (255,))
    c.d.rectangle((8 * SS, 21 * SS, 248 * SS, 21 * SS + SS - 1), fill=P["border"] + (255,))
    return c.result()


def bottom_background(P, bubble, home):
    c = Canvas(256, 192, P["bg"] + (255,))
    # Top bar with round shortcut buttons (settings on the left, home on the right)
    c.d.rectangle((0, 0, 256 * SS, 27 * SS), fill=P["bar"] + (255,))
    c.d.rectangle((0, 27 * SS, 256 * SS, 27 * SS + SS - 1), fill=P["border"] + (255,))
    c.circle(16, 13, 11, fill=P["panel"] + (255,))
    c.glyph(g_gear, (9, 6, 23, 20), P["glyph"])
    if home:
        c.circle(240, 13, 11, fill=P["panel"] + (255,))
        c.glyph(g_home, (233, 6, 247, 20), P["glyph"])
    # Footer
    c.d.rectangle((8 * SS, 168 * SS, 248 * SS, 168 * SS + SS - 1), fill=P["border"] + (255,))
    if bubble:
        # Title panel (the engine prints the game title in it)
        c.rrect((20, 34, 236, 88), 10, fill=P["panel"] + (255,))
        # START hint: (A) START
        c.circle(104, 180, 7, fill=P["text"] + (255,))
        c.text((104, 180), "A", 10, P["bg"] + (255,), anchor="mm")
        c.text((115, 180), "START", 11, P["text"] + (255,), anchor="lm")
    return c.result()


TILE = (8, 5, 56, 53)  # the game icon (32x32) is drawn at (16, 13) in the 64x64 box


def box_full(P):
    c = Canvas(64, 64)
    if P["shadow"]:
        c.rrect((TILE[0], TILE[1] + 2, TILE[2], TILE[3] + 2), 9, fill=(170, 170, 170, 255))
    c.rrect(TILE, 9, fill=P["panel"] + (255,), outline=P["border"] + (255,), width=1)
    return c.result()


def box_empty(P):
    c = Canvas(64, 64)
    c.rrect(TILE, 9, fill=P["empty"] + (255,))
    return c.result()


def folder(P):
    c = Canvas(64, 64)
    if P["shadow"]:
        c.rrect((TILE[0], TILE[1] + 2, TILE[2], TILE[3] + 2), 9, fill=(170, 170, 170, 255))
    top, bottom = (95, 170, 255), (45, 120, 230)
    for y in range(TILE[1] * SS, TILE[3] * SS):
        t = (y - TILE[1] * SS) / ((TILE[3] - TILE[1]) * SS)
        c.d.line((TILE[0] * SS, y, TILE[2] * SS - 1, y), fill=mix(top, bottom, t) + (255,))
    # Clip the gradient to a rounded tile
    m = Image.new("L", c.im.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((TILE[0] * SS, TILE[1] * SS, TILE[2] * SS - 1, TILE[3] * SS - 1), 9 * SS, fill=255)
    tile = Image.new("RGBA", c.im.size, (0, 0, 0, 0))
    tile.paste(c.im, (0, 0), m)
    if P["shadow"]:
        sh = Image.new("RGBA", c.im.size, (0, 0, 0, 0))
        ImageDraw.Draw(sh).rounded_rectangle((TILE[0] * SS, (TILE[1] + 2) * SS, TILE[2] * SS - 1, (TILE[3] + 2) * SS - 1), 9 * SS, fill=(170, 170, 170, 255))
        sh.alpha_composite(tile)
        tile = sh
    c.im = tile
    c.d = ImageDraw.Draw(c.im)
    c.glyph(g_folder, (18, 13, 46, 41), (255, 255, 255))
    return c.result()


def settings_icon(P):
    frames = []
    for _ in range(2):
        c = Canvas(64, 64)
        if P["shadow"]:
            c.rrect((TILE[0], TILE[1] + 3, TILE[2], TILE[3] + 3), 9, fill=(170, 170, 170, 255))
        # The engine draws this sprite one pixel higher than the boxes
        c.rrect((TILE[0], TILE[1] + 1, TILE[2], TILE[3] + 1), 9, fill=(100, 120, 140, 255))
        c.glyph(g_gear, (18, 15, 46, 43), (255, 255, 255))
        frames.append(c.result())
    out = Image.new("RGBA", (64, 128), (0, 0, 0, 0))
    out.paste(frames[0], (0, 0))
    out.paste(frames[1], (0, 64))
    return out


def cursor(P):
    """3 frames (32x64) of the left half of the selection frame; the engine
    mirrors it for the right half. Frames pulse from the accent colour to a
    lighter tint, like on the Switch."""
    out = Image.new("RGBA", (32, 192), (0, 0, 0, 0))
    for f, t in enumerate((0.0, 0.3, 0.6)):
        col = mix(P["accent"], (255, 255, 255), t)
        c = Canvas(64, 64)
        # 2px gap around the tile, 3px frame
        c.rrect((TILE[0] - 5, TILE[1] - 5, TILE[2] + 5, TILE[3] + 5), 14, outline=col + (255,), width=3)
        full = c.result()
        out.paste(full.crop((0, 0, 32, 64)), (0, f * 64))
    return out


def bubble_tip(P):
    c = Canvas(8, 8)
    c.d.polygon([(0, 0), (7 * SS, 0), (3.5 * SS, 5 * SS)], fill=P["panel"] + (255,))
    return c.result()


def dialog_box(P):
    c = Canvas(256, 192)
    c.rrect((6, 6, 250, 186), 14, fill=P["panel"] + (255,), outline=P["border"] + (255,), width=1)
    return c.result()


def shoulder(P, side, greyed):
    c = Canvas(78, 20)
    fill = P["panel"] if not greyed else mix(P["panel"], P["bg"], 0.6)
    fg = P["text"] if not greyed else mix(P["text"], P["bg"], 0.6)
    if side == "L":
        c.rrect((-12, 1, 74, 19), 9, fill=fill + (255,))
        c.circle(10, 10, 6, fill=fg + (255,))
        c.text((10, 10), "L", 9, fill + (255,), anchor="mm")
    else:
        c.rrect((4, 1, 90, 19), 9, fill=fill + (255,))
        c.circle(68, 10, 6, fill=fg + (255,))
        c.text((68, 10), "R", 9, fill + (255,), anchor="mm")
    return c.result()


def battery_icon(P, level, charging=False, low=False, purple=False):
    c = Canvas(18, 11)
    col = P["text"] + (255,)
    c.rrect((0, 0, 16, 11), 3, outline=col, width=1)
    c.d.rectangle((16 * SS, 3 * SS, 18 * SS - 1, 8 * SS - 1), fill=col)
    fill = (255, 75, 75, 255) if low else ((60, 200, 100, 255) if charging else ((180, 120, 255, 255) if purple else col))
    w = {0: 0, 1: 3, 2: 6, 3: 9, 4: 12}[level]
    if w:
        c.rrect((2, 2, 2 + w, 9), 1, fill=fill)
    if charging:
        c.d.polygon([(9 * SS, 1 * SS), (5 * SS, 6 * SS), (8 * SS, 6 * SS), (7 * SS, 10 * SS), (11 * SS, 5 * SS), (8 * SS, 5 * SS)], fill=P["bar"] + (255,))
    return c.result()


def volume_icon(P, level):
    c = Canvas(18, 12)
    col = P["text"] + (255,)
    c.d.polygon([(1 * SS, 4 * SS), (4 * SS, 4 * SS), (8 * SS, 1 * SS), (8 * SS, 11 * SS), (4 * SS, 8 * SS), (1 * SS, 8 * SS)], fill=col)
    for i in range(level if level < 4 else 3):
        r = 3 + i * 3
        c.d.arc(((8 - r) * SS, (6 - r) * SS, (8 + r) * SS, (6 + r) * SS), -50, 50, fill=col, width=int(1.4 * SS))
    if level == 0:
        c.d.line((11 * SS, 4 * SS, 16 * SS, 9 * SS), fill=col, width=int(1.4 * SS))
        c.d.line((16 * SS, 4 * SS, 11 * SS, 9 * SS), fill=col, width=int(1.4 * SS))
    return c.result()


def font_palette(fg, bg):
    """4 entries: transparent, then 3 levels of anti-aliasing towards fg."""
    return [0x0000, rgb15(mix(bg, fg, 0.35)), rgb15(mix(bg, fg, 0.7)), rgb15(fg)]


def theme_ini(P):
    pal = font_palette(P["text"], P["bg"])
    dis = font_palette(mix(P["text"], P["bg"], 0.5), P["bg"])
    title = font_palette(P["accent"] if P["dark"] else mix(P["accent"], (0, 0, 0), 0.25), P["panel"])
    dialog = font_palette(P["text"], P["panel"])
    bar = font_palette(P["text"], P["bar"])
    lines = [
        "; Generated by romsel_dsimenutheme/resources/modern theme/make_modern_theme.py",
        "[THEME]",
        "StartBorderRenderY\t= 96",
        "StartBorderSpriteW\t= 32",
        "StartBorderSpriteH\t= 64",
        "StartTextRenderY\t= 143",
        "",
        "BubbleTipRenderY\t= 88",
        "BubbleTipRenderX\t= 125",
        "BubbleTipSpriteH\t= 7",
        "BubbleTipSpriteW\t= 7",
        "",
        "TitleboxRenderY\t\t= 96",
        "TitleboxMaxLines\t= 3",
        "TitleboxTextY\t\t= 40",
        "TitleboxTextW\t\t= 200",
        "TitleboxTextLarge\t= 0",
        "",
        "VolumeRenderX\t\t= 6",
        "VolumeRenderY\t\t= 5",
        "UsernameRenderX\t\t= 28",
        "UsernameRenderY\t\t= 3",
        "DateRenderX\t\t= 150",
        "DateRenderY\t\t= 5",
        "TimeRenderX\t\t= 196",
        "TimeRenderY\t\t= 5",
        "BatteryRenderX\t\t= 232",
        "BatteryRenderY\t\t= 5",
        "",
        "ShoulderLRenderY\t= 172",
        "ShoulderLRenderX\t= 0",
        "ShoulderLTextY\t\t= 175",
        "ShoulderLTextX\t\t= 20",
        "ShoulderLTextAlign\t= 1",
        "ShoulderRRenderY\t= 172",
        "ShoulderRRenderX\t= 178",
        "ShoulderRTextY\t\t= 175",
        "ShoulderRTextX\t\t= 236",
        "ShoulderRTextAlign\t= -1",
        "",
    ]
    for name, p in (("FontPalette", pal), ("FontPaletteDisabled", dis), ("FontPaletteTitlebox", title),
                    ("FontPaletteDialog", dialog), ("FontPaletteOverlay", pal), ("FontPaletteUsername", bar),
                    ("FontPaletteDateTime", bar)):
        for i, v in enumerate(p):
            lines.append(f"{name}{i + 1}\t= 0x{v:04X}")
    lines += [
        "",
        "UsernameUserPalette\t= 0",
        "UsernameEdgeAlpha\t= 0",
        "PurpleBatteryAvailable\t= 1",
        "PlayStartupJingle\t= 1",
        "PlayLidSound\t\t= 1",
        "RotatingCubesRenderY\t= 78",
        "RenderPhoto\t\t= 0",
        "; The selection frame and boxes use their own colours",
        "CursorUserPalette\t= 0",
        "BoxFullUserPalette\t= 0",
        "BoxEmptyUserPalette\t= 0",
        "FolderUserPalette\t= 0",
        "BubbleUserPalette\t= 0",
        "DialogBoxUserPalette\t= 0",
        "StartBorderUserPalette\t= 0",
        "",
    ]
    return "\n".join(lines)


def generate(name, P, preview):
    out = os.path.join(OUT_BASE, name)
    for d in ("background", "grf", "ui", "battery", "volume"):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    save_png_opaque(top_background(P), os.path.join(out, "background", "top.png"), P["bg"])
    for suffix, home in (("", True), ("_ds", False)):
        save_png_opaque(bottom_background(P, False, home), os.path.join(out, "background", f"bottom{suffix}.png"), P["bg"])
        save_png_opaque(bottom_background(P, True, home), os.path.join(out, "background", f"bottom_bubble{suffix}.png"), P["bg"])
    save_bmp4(box_full(P), os.path.join(out, "grf", "box_full.bmp"), P["bg"])
    save_bmp4(box_empty(P), os.path.join(out, "grf", "box_empty.bmp"), P["bg"])
    save_bmp4(folder(P), os.path.join(out, "grf", "folder.bmp"), P["bg"])
    save_bmp4(settings_icon(P), os.path.join(out, "grf", "icon_settings.bmp"), P["bg"])
    save_bmp4(cursor(P), os.path.join(out, "grf", "cursor.bmp"), P["bg"])
    save_bmp4(bubble_tip(P), os.path.join(out, "grf", "bubble.bmp"), P["bg"])
    save_bmp4(dialog_box(P), os.path.join(out, "grf", "dialogbox.bmp"), mix(P["bg"], (0, 0, 0), 0.4))
    for side in ("L", "R"):
        save_png_keyed(shoulder(P, side, False), os.path.join(out, "ui", f"{side}shoulder.png"), P["bg"])
        save_png_keyed(shoulder(P, side, True), os.path.join(out, "ui", f"{side}shoulder_greyed.png"), P["bg"])
    bat = {
        "battery0": dict(level=0), "battery1": dict(level=1, low=True), "battery2": dict(level=2),
        "battery3": dict(level=3), "battery4": dict(level=4), "batteryfull": dict(level=4),
        "batteryfullDS": dict(level=4), "batterylow": dict(level=1, low=True),
        "batterycharge": dict(level=4, charging=True), "batterychargeblink": dict(level=2, charging=True),
        "battery1purple": dict(level=1, purple=True), "battery2purple": dict(level=2, purple=True),
        "battery3purple": dict(level=3, purple=True), "battery4purple": dict(level=4, purple=True),
    }
    for n, kw in bat.items():
        save_png_keyed(battery_icon(P, **kw), os.path.join(out, "battery", n + ".png"), P["bar"])
    for lv in range(5):
        save_png_keyed(volume_icon(P, lv), os.path.join(out, "volume", f"volume{lv}.png"), P["bar"])
    with open(os.path.join(out, "theme.ini"), "w") as f:
        f.write(theme_ini(P))
    print("generated", out)
    if preview:
        make_preview(name, P, out)


def make_preview(name, P, out):
    """Rough composite of both screens as the engine draws them."""
    top = Image.open(os.path.join(out, "background", "top.png")).convert("RGBA")
    d = ImageDraw.Draw(top)
    f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11)
    d.text((28, 4), "Nickname", font=f, fill=P["text"])
    d.text((150, 5), "09/27", font=f, fill=P["text"])
    d.text((196, 5), "14:05", font=f, fill=P["text"])
    top.alpha_composite(Image.open(os.path.join(out, "battery", "battery3.png")).convert("RGBA"), (232, 5))
    top.alpha_composite(Image.open(os.path.join(out, "volume", "volume3.png")).convert("RGBA"), (6, 5))
    bot = Image.open(os.path.join(out, "background", "bottom_bubble.png")).convert("RGBA")

    def bmp(path):
        im = Image.open(path).convert("RGBA")
        px = im.load()
        for y in range(im.size[1]):
            for x in range(im.size[0]):
                if px[x, y][:3] == (255, 0, 255):
                    px[x, y] = (0, 0, 0, 0)
        return im

    box = bmp(os.path.join(out, "grf", "box_full.bmp"))
    empty = bmp(os.path.join(out, "grf", "box_empty.bmp"))
    fold = bmp(os.path.join(out, "grf", "folder.bmp"))
    curs = bmp(os.path.join(out, "grf", "cursor.bmp")).crop((0, 0, 32, 64))
    tip = bmp(os.path.join(out, "grf", "bubble.bmp"))
    icon = Image.new("RGBA", (32, 32), (220, 60, 60, 255))
    ImageDraw.Draw(icon).rectangle((6, 6, 25, 25), fill=(255, 220, 0, 255))
    for i, spr in enumerate([fold, box, box, box, empty]):
        x = 96 + (i - 1) * 58
        bot.alpha_composite(spr, (x, 96))
        if spr is box:
            bot.alpha_composite(icon, (x + 16, 96 + 13))
    bot.alpha_composite(curs, (96, 96))
    bot.alpha_composite(curs.transpose(Image.FLIP_LEFT_RIGHT), (128, 96))
    bot.alpha_composite(tip, (125, 88))
    d = ImageDraw.Draw(bot)
    ft = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11)
    title_col = P["accent"] if P["dark"] else mix(P["accent"], (0, 0, 0), 0.25)
    d.text((128, 48), "Mario Kart DS", font=ft, fill=title_col, anchor="mm")
    d.text((128, 62), "Nintendo", font=ft, fill=title_col, anchor="mm")
    bot.alpha_composite(Image.open(os.path.join(out, "ui", "Lshoulder.png")).convert("RGBA"), (0, 172))
    bot.alpha_composite(Image.open(os.path.join(out, "ui", "Rshoulder.png")).convert("RGBA"), (178, 172))
    sheet = Image.new("RGB", (256, 192 * 2 + 8), (20, 20, 20))
    sheet.paste(top.convert("RGB"), (0, 0))
    sheet.paste(bot.convert("RGB"), (0, 200))
    dest = os.environ.get("PREVIEW_DIR", tempfile.gettempdir())
    sheet.resize((512, 400 * 2 - 8), Image.NEAREST).save(os.path.join(dest, name.replace(" ", "_") + "_preview.png"))


def main():
    preview = "--preview" in sys.argv
    for name, P in VARIANTS.items():
        generate(name, P, preview)


if __name__ == "__main__":
    main()

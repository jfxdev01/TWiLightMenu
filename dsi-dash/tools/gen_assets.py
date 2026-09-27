# Gera os assets do DSi Dash (fontes, icones, icone do banner).
#   python tools/gen_assets.py
# Saidas:
#   arm9/data/font_*.bin, arm9/data/icons.bin  (embutidos via bin2o)
#   arm9/source/assets_gen.h                   (ids de icones / glifos extras)
#   icon.bmp                                   (icone 32x32 16 cores do banner .nds)
#   tools/preview_*.png                        (previas para conferir)
import math
import os
import struct

from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "arm9", "data")
SRC = os.path.join(ROOT, "arm9", "source")
FONT_TTF = os.path.join(ROOT, "assets", "Nunito.ttf")
os.makedirs(DATA, exist_ok=True)

# ---------------------------------------------------------------------------
# Fontes
# ---------------------------------------------------------------------------
# Slots 0..223 = U+0020..U+00FF; depois os extras abaixo (mesma ordem no C).
EXTRA_CPS = [
    0x2022, 0x2026, 0x201C, 0x201D, 0x2018, 0x2019, 0x2013, 0x2014,
    0x20AC, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2264, 0x2265,
]

FONTS = [
    # nome,   px, peso, apenas_estes_chars
    ("small", 11, 600, None),
    ("body",  13, 600, None),
    ("title", 16, 800, None),
    ("big",   24, 800, None),
    ("huge",  46, 700, "0123456789:-. °"),
    ("text",  12, 500, None),
    ("textb", 12, 800, None),
    ("h1",    19, 800, None),
    ("h2",    15, 800, None),
]


def load_font(px, weight):
    f = ImageFont.truetype(FONT_TTF, px)
    try:
        f.set_variation_by_axes([weight])
    except Exception as e:  # fonte sem eixo variavel
        print("aviso: sem variacao de peso:", e)
    return f


def render_glyph(font, ch):
    # renderiza com bbox exato; retorna (w,h,xoff,yoff,adv,alpha_bytes)
    bbox = font.getbbox(ch, anchor="ls")  # relativo a baseline/esquerda
    adv = round(font.getlength(ch))
    x0, y0, x1, y1 = bbox
    w, h = x1 - x0, y1 - y0
    if w <= 0 or h <= 0:
        return 0, 0, 0, 0, adv, b""
    img = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(img)
    d.text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
    return w, h, x0, y0, adv, img.tobytes()


def build_font(name, px, weight, only):
    font = load_font(px, weight)
    ascent, descent = font.getmetrics()
    line_h = ascent + descent
    cps = list(range(0x20, 0x100)) + EXTRA_CPS
    glyphs = []
    bitmap = bytearray()
    preview = []
    for cp in cps:
        ch = chr(cp)
        present = (only is None or ch in only) and not (0x7F <= cp < 0xA0)
        if present:
            w, h, xo, yo, adv, alpha = render_glyph(font, ch)
        else:
            w = h = xo = yo = adv = 0
            alpha = b""
        off = len(bitmap)
        # 4bpp, 2 pixels por byte (nibble baixo = pixel da esquerda)
        rowb = (w + 1) // 2
        for y in range(h):
            for xb in range(rowb):
                a0 = alpha[y * w + xb * 2] >> 4
                a1 = alpha[y * w + xb * 2 + 1] >> 4 if xb * 2 + 1 < w else 0
                bitmap.append(a0 | (a1 << 4))
        # yoff relativo ao topo da linha (ascent)
        glyphs.append((off, w, h, xo, yo + ascent, adv, 1 if present else 0))
        if present and w:
            preview.append((ch, w, h, alpha))
    # cabecalho: 'DFN1', lineH, ascent, count, reservado
    out = bytearray(b"DFN1")
    out += struct.pack("<BBHI", line_h, ascent, len(glyphs), 0)
    for off, w, h, xo, yo, adv, pres in glyphs:
        out += struct.pack("<IBBbbBBH", off, w, h, xo, yo, adv, pres, 0)
    out += bitmap
    with open(os.path.join(DATA, f"font_{name}.bin"), "wb") as fp:
        fp.write(out)
    return line_h, ascent, len(out)


def font_preview():
    img = Image.new("RGB", (512, 360), (235, 235, 235))
    d = ImageDraw.Draw(img)
    y = 6
    samples = {
        "small": "Navegador: páginas, notícias e busca — ação ção ÇÃO 0123",
        "body": "Clima agora · Maceió · Previsão de 7 dias",
        "title": "Configurações  Álbum  Câmera",
        "big": "25° Parcialmente nublado",
        "huge": "17:29",
    }
    for name, px, weight, only in FONTS:
        f = load_font(px, weight)
        d.text((6, y), samples.get(name, "Texto da p00e1gina web: a00e700e3o, cora00e700e3o"), font=f, fill=(45, 45, 45))
        y += px + 14
    img = img.resize((1024, 720), Image.NEAREST)
    img.save(os.path.join(ROOT, "tools", "preview_fonts.png"))


# ---------------------------------------------------------------------------
# Icones (mascaras alfa, desenhadas em supersampling 8x)
# ---------------------------------------------------------------------------
SS = 8


class Pen:
    def __init__(self, size):
        self.size = size
        self.S = size * SS
        self.img = Image.new("L", (self.S, self.S), 0)
        self.d = ImageDraw.Draw(self.img)

    def p(self, v):
        return v * self.S

    def pt(self, x, y):
        return (x * self.S, y * self.S)

    def circle(self, cx, cy, r, fill=255):
        S = self.S
        self.d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=fill)

    def ring(self, cx, cy, r, w, fill=255):
        self.circle(cx, cy, r + w / 2, fill)
        self.circle(cx, cy, r - w / 2, 0 if fill else 255)

    def rrect(self, x0, y0, x1, y1, r, fill=255):
        S = self.S
        self.d.rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], radius=r * S, fill=fill)

    def rrect_outline(self, x0, y0, x1, y1, r, w, fill=255):
        S = self.S
        self.d.rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], radius=r * S, outline=fill, width=max(1, int(w * S)))

    def rect(self, x0, y0, x1, y1, fill=255):
        S = self.S
        self.d.rectangle([x0 * S, y0 * S, x1 * S, y1 * S], fill=fill)

    def line(self, pts, w, fill=255, caps=True):
        S = self.S
        pp = [(x * S, y * S) for x, y in pts]
        self.d.line(pp, fill=fill, width=max(1, int(w * S)), joint="curve")
        if caps:
            for x, y in pts:
                self.circle(x, y, w / 2, fill)

    def poly(self, pts, fill=255):
        self.d.polygon([(x * self.S, y * self.S) for x, y in pts], fill=fill)

    def arc(self, cx, cy, r, a0, a1, w, fill=255, caps=True):
        S = self.S
        self.d.arc([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], a0, a1, fill=fill, width=max(1, int(w * S)))
        if caps:
            for a in (a0, a1):
                t = math.radians(a)
                self.circle(cx + r * math.cos(t) - 0, cy + r * math.sin(t), w / 2 * 0.98, fill)

    def result(self):
        return self.img.resize((self.size, self.size), Image.LANCZOS)


def ic_weather(p):
    # sol atras de nuvem
    cx, cy = 0.38, 0.38
    p.circle(cx, cy, 0.14)
    for i in range(8):
        a = math.radians(i * 45)
        p.line([(cx + 0.2 * math.cos(a), cy + 0.2 * math.sin(a)),
                (cx + 0.27 * math.cos(a), cy + 0.27 * math.sin(a))], 0.055)
    # borda da nuvem (apaga) e nuvem
    def cloud(fill, grow):
        p.circle(0.46, 0.62, 0.13 + grow, fill)
        p.circle(0.62, 0.54, 0.17 + grow, fill)
        p.circle(0.76, 0.64, 0.12 + grow, fill)
        p.rrect(0.33 - grow, 0.62 - grow, 0.88 + grow, 0.76 + grow, 0.07 + grow, fill)
    cloud(0, 0.05)
    cloud(255, 0)


def ic_browser(p):
    cx, cy, r = 0.5, 0.5, 0.36
    p.ring(cx, cy, r, 0.07)  # antes das linhas: ring() apaga o interior
    layer = Pen(p.size)
    S = p.S
    layer.d.ellipse([(cx - 0.16) * S, (cy - r) * S, (cx + 0.16) * S, (cy + r) * S], outline=255, width=int(0.06 * S))
    layer.line([(cx, cy - r), (cx, cy + r)], 0.06, caps=False)
    layer.line([(cx - r, cy), (cx + r, cy)], 0.06, caps=False)
    layer.line([(cx - r, cy - 0.17), (cx + r, cy - 0.17)], 0.05, caps=False)
    layer.line([(cx - r, cy + 0.17), (cx + r, cy + 0.17)], 0.05, caps=False)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=255)
    p.img = ImageChops.lighter(p.img, ImageChops.multiply(layer.img, mask))
    p.d = ImageDraw.Draw(p.img)


def ic_camera(p):
    p.rrect(0.12, 0.3, 0.88, 0.8, 0.1)
    p.rrect(0.34, 0.2, 0.62, 0.36, 0.04)
    p.circle(0.5, 0.55, 0.19, 0)
    p.circle(0.5, 0.55, 0.11)
    p.circle(0.76, 0.4, 0.04, 0)


def ic_album(p):
    p.rrect_outline(0.12, 0.2, 0.88, 0.8, 0.08, 0.07)
    p.poly([(0.2, 0.73), (0.42, 0.45), (0.55, 0.61), (0.65, 0.5), (0.8, 0.73)])
    p.circle(0.67, 0.36, 0.07)


def ic_news(p):
    p.rrect_outline(0.16, 0.16, 0.84, 0.84, 0.07, 0.065)
    p.rrect(0.27, 0.28, 0.73, 0.41, 0.03)
    for y in (0.52, 0.62, 0.72):
        p.line([(0.29, y), (0.71, y)], 0.05)


def ic_transfer(p):
    w = 0.08
    p.line([(0.36, 0.8), (0.36, 0.26)], w)
    p.line([(0.2, 0.42), (0.36, 0.25), (0.52, 0.42)], w)
    p.line([(0.64, 0.2), (0.64, 0.74)], w)
    p.line([(0.48, 0.58), (0.64, 0.75), (0.8, 0.58)], w)


def ic_files(p):
    p.poly([(0.12, 0.28), (0.4, 0.28), (0.47, 0.36), (0.12, 0.36)])
    p.rrect(0.12, 0.26, 0.42, 0.4, 0.05)
    p.rrect(0.12, 0.34, 0.88, 0.78, 0.07)
    p.rect(0.12, 0.34, 0.88, 0.4, 0)
    p.rrect(0.12, 0.4, 0.88, 0.78, 0.07)


def ic_games(p):
    p.rrect(0.08, 0.3, 0.92, 0.72, 0.2)
    # direcional (apaga)
    p.rect(0.21, 0.47, 0.39, 0.55, 0)
    p.rect(0.26, 0.42, 0.34, 0.6, 0)
    p.circle(0.66, 0.45, 0.045, 0)
    p.circle(0.76, 0.56, 0.045, 0)


def ic_wiki(p):
    p.line([(0.14, 0.3), (0.32, 0.74), (0.5, 0.36), (0.68, 0.74), (0.86, 0.3)], 0.08)


def ic_settings(p):
    cx, cy = 0.5, 0.5
    p.circle(cx, cy, 0.27)
    for i in range(8):
        a = i * math.pi / 4
        pts = []
        for da, rr in ((-0.2, 0.25), (-0.13, 0.39), (0.13, 0.39), (0.2, 0.25)):
            pts.append((cx + rr * math.cos(a + da), cy + rr * math.sin(a + da)))
        p.poly(pts)
    p.circle(cx, cy, 0.12, 0)


def ic_wifi(p):
    cx, cy = 0.5, 0.78
    p.circle(cx, cy, 0.07)
    for r in (0.24, 0.42):
        p.arc(cx, cy, r, 225, 315, 0.09)


def ic_power(p):
    p.arc(0.5, 0.53, 0.28, -50, 230, 0.08)
    p.line([(0.5, 0.18), (0.5, 0.48)], 0.08)


def ic_info(p):
    p.ring(0.5, 0.5, 0.34, 0.07)
    p.circle(0.5, 0.33, 0.055)
    p.line([(0.5, 0.47), (0.5, 0.7)], 0.09)


def ic_theme(p):
    # meia lua / sol (tema claro/escuro)
    p.circle(0.5, 0.5, 0.3)
    p.circle(0.62, 0.4, 0.24, 0)


def ic_back(p):
    p.line([(0.62, 0.2), (0.32, 0.5), (0.62, 0.8)], 0.11)


def ic_fwd(p):
    p.line([(0.38, 0.2), (0.68, 0.5), (0.38, 0.8)], 0.11)


def ic_search(p):
    p.ring(0.43, 0.43, 0.22, 0.09)
    p.line([(0.6, 0.6), (0.8, 0.8)], 0.11)


def ic_star(p):
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        r = 0.38 if i % 2 == 0 else 0.16
        pts.append((0.5 + r * math.cos(a), 0.54 + r * math.sin(a)))
    p.poly(pts)


def ic_refresh(p):
    p.arc(0.5, 0.5, 0.28, 30, 320, 0.09)
    a = math.radians(320)
    x, y = 0.5 + 0.28 * math.cos(a), 0.5 + 0.28 * math.sin(a)
    p.poly([(x - 0.15, y - 0.02), (x + 0.07, y - 0.14), (x + 0.07, y + 0.1)])


def ic_home(p):
    p.poly([(0.5, 0.16), (0.86, 0.48), (0.14, 0.48)])
    p.rrect(0.24, 0.44, 0.76, 0.84, 0.05)
    p.rrect(0.42, 0.6, 0.58, 0.84, 0.03, 0)


def ic_close(p):
    p.line([(0.25, 0.25), (0.75, 0.75)], 0.11)
    p.line([(0.75, 0.25), (0.25, 0.75)], 0.11)


def ic_check(p):
    p.line([(0.2, 0.52), (0.42, 0.74), (0.8, 0.3)], 0.11)


def ic_bksp(p):
    p.poly([(0.08, 0.5), (0.3, 0.24), (0.9, 0.24), (0.9, 0.76), (0.3, 0.76)])
    p.line([(0.45, 0.38), (0.7, 0.62)], 0.08, 0)
    p.line([(0.7, 0.38), (0.45, 0.62)], 0.08, 0)


def ic_shift(p):
    p.poly([(0.5, 0.14), (0.86, 0.52), (0.66, 0.52), (0.66, 0.84), (0.34, 0.84), (0.34, 0.52), (0.14, 0.52)])


def ic_file(p):
    p.poly([(0.22, 0.12), (0.6, 0.12), (0.8, 0.32), (0.8, 0.88), (0.22, 0.88)])
    p.poly([(0.6, 0.12), (0.6, 0.32), (0.8, 0.32)], 0)
    p.poly([(0.6, 0.16), (0.6, 0.3), (0.74, 0.3)])


def ic_folder_s(p):
    ic_files(p)


def ic_image(p):
    ic_album(p)


def ic_lock(p):
    p.arc(0.5, 0.42, 0.17, 180, 360, 0.08, caps=False)
    p.rect(0.33, 0.42 - 0.001, 0.37, 0.5)
    p.rect(0.63, 0.42 - 0.001, 0.67, 0.5)
    p.rrect(0.22, 0.46, 0.78, 0.86, 0.07)
    p.circle(0.5, 0.63, 0.06, 0)


def ic_shutter(p):
    p.circle(0.5, 0.5, 0.42)
    p.circle(0.5, 0.5, 0.34, 0)
    p.circle(0.5, 0.5, 0.29)


def ic_swapcam(p):
    p.arc(0.5, 0.5, 0.3, 200, 340, 0.08)
    p.poly([(0.72, 0.3), (0.9, 0.44), (0.7, 0.5)])
    p.arc(0.5, 0.5, 0.3, 20, 160, 0.08)
    p.poly([(0.28, 0.7), (0.1, 0.56), (0.3, 0.5)])


def ic_qr(p):
    for (x, y) in ((0.14, 0.14), (0.58, 0.14), (0.14, 0.58)):
        p.rect(x, y, x + 0.28, y + 0.28)
        p.rect(x + 0.06, y + 0.06, x + 0.22, y + 0.22, 0)
        p.rect(x + 0.1, y + 0.1, x + 0.18, y + 0.18)
    for (x, y) in ((0.6, 0.6), (0.74, 0.6), (0.6, 0.74), (0.74, 0.74), (0.67, 0.67)):
        p.rect(x, y, x + 0.1, y + 0.1)


def ic_trash(p):
    p.rrect(0.2, 0.22, 0.8, 0.3, 0.03)
    p.rrect(0.4, 0.14, 0.6, 0.24, 0.03)
    p.poly([(0.25, 0.34), (0.75, 0.34), (0.7, 0.88), (0.3, 0.88)])
    for x in (0.4, 0.5, 0.6):
        p.line([(x, 0.44), (x, 0.78)], 0.045, 0)


def ic_notes(p):
    p.rrect(0.2, 0.14, 0.8, 0.86, 0.07)
    for y in (0.34, 0.48, 0.62):
        p.line([(0.32, y), (0.68, y)], 0.05, 0)


def ic_calc(p):
    p.rrect(0.22, 0.12, 0.78, 0.88, 0.08)
    p.rrect(0.3, 0.2, 0.7, 0.36, 0.03, 0)
    for r in range(3):
        for c in range(3):
            x = 0.33 + c * 0.13
            y = 0.46 + r * 0.13
            p.rect(x, y, x + 0.08, y + 0.08, 0)


def ic_clock(p):
    p.ring(0.5, 0.5, 0.34, 0.08)
    p.line([(0.5, 0.5), (0.5, 0.3)], 0.08)
    p.line([(0.5, 0.5), (0.65, 0.58)], 0.08)


def ic_radio(p):
    p.rrect(0.1, 0.34, 0.9, 0.84, 0.08)
    p.line([(0.3, 0.3), (0.72, 0.14)], 0.05)
    p.circle(0.34, 0.59, 0.14, 0)
    p.circle(0.34, 0.59, 0.07)
    p.rect(0.58, 0.46, 0.8, 0.5, 0)
    p.rect(0.58, 0.56, 0.8, 0.6, 0)
    p.rect(0.58, 0.66, 0.8, 0.7, 0)


def ic_map(p):
    # mapa dobrado com pino
    p.poly([(0.1, 0.26), (0.36, 0.16), (0.64, 0.26), (0.9, 0.16), (0.9, 0.78), (0.64, 0.88), (0.36, 0.78), (0.1, 0.88)])
    p.line([(0.36, 0.2), (0.36, 0.8)], 0.04, 0, caps=False)
    p.line([(0.64, 0.28), (0.64, 0.86)], 0.04, 0, caps=False)
    p.circle(0.5, 0.42, 0.16, 0)
    p.circle(0.5, 0.42, 0.12)
    p.poly([(0.4, 0.48), (0.6, 0.48), (0.5, 0.68)])
    p.circle(0.5, 0.42, 0.05, 0)


def ic_translate(p):
    p.rrect(0.08, 0.12, 0.6, 0.56, 0.08)
    p.poly([(0.18, 0.54), (0.3, 0.54), (0.18, 0.68)])
    p.rrect(0.4, 0.42, 0.92, 0.86, 0.08)
    p.rrect(0.44, 0.46, 0.88, 0.82, 0.06, 0)
    p.poly([(0.78, 0.84), (0.84, 0.84), (0.84, 0.94)])
    # "A" no primeiro balao (apagado) e linhas no segundo
    p.line([(0.22, 0.46), (0.34, 0.2), (0.46, 0.46)], 0.06, 0)
    p.line([(0.27, 0.37), (0.41, 0.37)], 0.05, 0)
    for y in (0.56, 0.64, 0.72):
        p.line([(0.52, y), (0.8, y)], 0.045)


def ic_pin(p):
    p.circle(0.5, 0.38, 0.28)
    p.poly([(0.26, 0.5), (0.74, 0.5), (0.5, 0.92)])
    p.circle(0.5, 0.38, 0.11, 0)


def ic_plus(p):
    p.line([(0.5, 0.2), (0.5, 0.8)], 0.12)
    p.line([(0.2, 0.5), (0.8, 0.5)], 0.12)


def ic_minus(p):
    p.line([(0.2, 0.5), (0.8, 0.5)], 0.12)


def ic_target(p):
    p.ring(0.5, 0.5, 0.26, 0.08)
    p.circle(0.5, 0.5, 0.09)
    for a, b in (((0.5, 0.06), (0.5, 0.22)), ((0.5, 0.78), (0.5, 0.94)), ((0.06, 0.5), (0.22, 0.5)), ((0.78, 0.5), (0.94, 0.5))):
        p.line([a, b], 0.08)


def ic_race(p):
    # carro de corrida visto de lado + bandeira quadriculada
    p.poly([(0.06, 0.62), (0.12, 0.5), (0.34, 0.46), (0.46, 0.34), (0.7, 0.34), (0.82, 0.46), (0.95, 0.5), (0.96, 0.64), (0.06, 0.66)])
    p.poly([(0.5, 0.38), (0.66, 0.38), (0.74, 0.47), (0.44, 0.47)], 0)
    for x in (0.27, 0.77):
        p.circle(x, 0.66, 0.11, 0)
        p.circle(x, 0.66, 0.085)
        p.circle(x, 0.66, 0.035, 0)
    for k in range(4):
        for j in range(2):
            if (k + j) % 2 == 0:
                p.rect(0.12 + k * 0.07, 0.1 + j * 0.07, 0.19 + k * 0.07, 0.17 + j * 0.07)
    p.rect(0.1, 0.1, 0.12, 0.34)


# weather parts (desenhados coloridos em runtime)
def wx_sun(p):
    cx, cy = 0.5, 0.5
    p.circle(cx, cy, 0.2)
    for i in range(8):
        a = math.radians(i * 45)
        p.line([(cx + 0.29 * math.cos(a), cy + 0.29 * math.sin(a)),
                (cx + 0.4 * math.cos(a), cy + 0.4 * math.sin(a))], 0.07)


def wx_cloud(p):
    p.circle(0.34, 0.6, 0.16)
    p.circle(0.55, 0.47, 0.22)
    p.circle(0.74, 0.62, 0.14)
    p.rrect(0.18, 0.6, 0.88, 0.76, 0.08)


def wx_rain(p):
    for x, y in ((0.3, 0.2), (0.5, 0.3), (0.7, 0.2), (0.4, 0.62), (0.6, 0.66)):
        p.line([(x + 0.06, y), (x - 0.02, y + 0.2)], 0.07)


def wx_snow(p):
    for x, y in ((0.3, 0.3), (0.62, 0.3), (0.46, 0.66)):
        for a in (0, 60, 120):
            t = math.radians(a)
            p.line([(x - 0.1 * math.cos(t), y - 0.1 * math.sin(t)), (x + 0.1 * math.cos(t), y + 0.1 * math.sin(t))], 0.05)


def wx_bolt(p):
    p.poly([(0.56, 0.08), (0.28, 0.54), (0.48, 0.54), (0.4, 0.92), (0.72, 0.42), (0.52, 0.42), (0.64, 0.08)])


def wx_fog(p):
    for y, x0, x1 in ((0.3, 0.16, 0.84), (0.5, 0.1, 0.78), (0.7, 0.22, 0.9)):
        p.line([(x0, y), (x1, y)], 0.08)


def wx_moon(p):
    p.circle(0.5, 0.5, 0.3)
    p.circle(0.66, 0.38, 0.26, 0)


ICONS = [
    # (id, funcao, tamanhos)
    ("WEATHER", ic_weather, (40, 20)),
    ("BROWSER", ic_browser, (40, 20)),
    ("CAMERA", ic_camera, (40, 20)),
    ("ALBUM", ic_album, (40, 20)),
    ("NEWS", ic_news, (40, 20)),
    ("TRANSFER", ic_transfer, (40, 20)),
    ("FILES", ic_files, (40, 20, 14)),
    ("GAMES", ic_games, (40, 20)),
    ("WIKI", ic_wiki, (40, 20)),
    ("SETTINGS", ic_settings, (40, 20)),
    ("WIFI", ic_wifi, (40, 20)),
    ("POWER", ic_power, (20,)),
    ("INFO", ic_info, (20,)),
    ("THEME", ic_theme, (20,)),
    ("NOTES", ic_notes, (40, 20)),
    ("CALC", ic_calc, (40, 20)),
    ("CLOCK", ic_clock, (40, 20)),
    ("RADIO", ic_radio, (40, 20)),
    ("BACK", ic_back, (20, 14)),
    ("FWD", ic_fwd, (20, 14)),
    ("SEARCH", ic_search, (20, 14)),
    ("STAR", ic_star, (20, 14)),
    ("REFRESH", ic_refresh, (20, 14)),
    ("HOME", ic_home, (20, 14)),
    ("CLOSE", ic_close, (20, 14)),
    ("CHECK", ic_check, (20, 14)),
    ("BKSP", ic_bksp, (20,)),
    ("SHIFT", ic_shift, (20,)),
    ("FILE", ic_file, (14,)),
    ("IMAGE", ic_image, (14,)),
    ("LOCK", ic_lock, (14,)),
    ("SHUTTER", ic_shutter, (40,)),
    ("SWAPCAM", ic_swapcam, (20,)),
    ("QR", ic_qr, (20,)),
    ("TRASH", ic_trash, (20,)),
    ("MAP", ic_map, (40, 20)),
    ("RACE", ic_race, (40, 20)),
    ("TRANSLATE", ic_translate, (40, 20)),
    ("PIN", ic_pin, (20,)),
    ("PLUS", ic_plus, (20,)),
    ("MINUS", ic_minus, (20,)),
    ("TARGET", ic_target, (20,)),
    ("WX_SUN", wx_sun, (48, 24)),
    ("WX_CLOUD", wx_cloud, (48, 24)),
    ("WX_RAIN", wx_rain, (48, 24)),
    ("WX_SNOW", wx_snow, (48, 24)),
    ("WX_BOLT", wx_bolt, (48, 24)),
    ("WX_FOG", wx_fog, (48, 24)),
    ("WX_MOON", wx_moon, (48, 24)),
]


def build_icons():
    entries = []
    blob = bytearray()
    names = []
    sheet = []
    for name, fn, sizes in ICONS:
        for sz in sizes:
            p = Pen(sz)
            fn(p)
            img = p.result()
            off = len(blob)
            blob += img.tobytes()
            entries.append((off, sz, sz))
            names.append(f"IC_{name}_{sz}")
            sheet.append(img)
    out = bytearray(b"DIC1")
    out += struct.pack("<I", len(entries))
    for off, w, h in entries:
        out += struct.pack("<IHH", off, w, h)
    out += blob
    with open(os.path.join(DATA, "icons.bin"), "wb") as fp:
        fp.write(out)
    # folha de previa
    W = 64 * 12
    rows = (len(sheet) + 11) // 12
    pv = Image.new("RGB", (W, rows * 64), (0, 150, 190))
    for i, im in enumerate(sheet):
        x, y = (i % 12) * 64 + 8, (i // 12) * 64 + 8
        pv.paste((255, 255, 255), (x, y, x + im.width, y + im.height), im)
    pv.save(os.path.join(ROOT, "tools", "preview_icons.png"))
    return names, len(out)


def build_banner_icon():
    # 32x32, 16 cores: quadrado arredondado com gradiente ciano e grade 2x2 branca
    S = 32 * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for y in range(S):
        t = y / S
        c = (int(30 + 0 * t), int(200 - 60 * t), int(235 - 20 * t), 255)
        d.line([(0, y), (S, y)], fill=c)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([SS, SS, S - SS, S - SS], radius=7 * SS, fill=255)
    img.putalpha(mask)
    for (x, y) in ((8, 8), (17, 8), (8, 17), (17, 17)):
        d.rounded_rectangle([x * SS, y * SS, (x + 7) * SS, (y + 7) * SS], radius=2 * SS, fill=(255, 255, 255, 255))
    img = img.resize((32, 32), Image.LANCZOS)
    bg = Image.new("RGB", (32, 32), (255, 0, 255))  # cor 0 = transparente
    bg.paste(img, (0, 0), img)
    q = bg.quantize(colors=15, method=Image.Quantize.MEDIANCUT)
    pal = q.getpalette()[: 15 * 3]
    px = list(q.getdata())
    # garante magenta como indice 0
    palette = [(255, 0, 255)] + [tuple(pal[i * 3: i * 3 + 3]) for i in range(15)]
    alpha = img.split()[3]
    idx = []
    for i, v in enumerate(px):
        idx.append(0 if alpha.getpixel((i % 32, i // 32)) < 128 else v + 1)
    # BMP 4bpp
    row = 16  # 32 px * 4 bits = 16 bytes (ja multiplo de 4)
    pix = bytearray()
    for y in range(31, -1, -1):
        for x in range(0, 32, 2):
            pix.append((idx[y * 32 + x] << 4) | idx[y * 32 + x + 1])
    pal_bytes = bytearray()
    for r, g, b in palette:
        pal_bytes += bytes((b, g, r, 0))
    hdr_size = 14 + 40 + len(pal_bytes)
    bmp = bytearray(b"BM")
    bmp += struct.pack("<IHHI", hdr_size + len(pix), 0, 0, hdr_size)
    bmp += struct.pack("<IiiHHIIiiII", 40, 32, 32, 1, 4, 0, len(pix), 2835, 2835, 16, 16)
    bmp += pal_bytes + pix
    with open(os.path.join(ROOT, "icon.bmp"), "wb") as fp:
        fp.write(bmp)


def main():
    lines = ["// gerado por tools/gen_assets.py — nao editar", "#pragma once", ""]
    lines.append("// fontes")
    for i, (name, px, w, only) in enumerate(FONTS):
        lh, asc, size = build_font(name, px, w, only)
        print(f"font {name}: {px}px line={lh} asc={asc} {size} bytes")
        lines.append(f"#define FONT_{name.upper()} {i}")
    lines.append(f"#define FONT_COUNT {len(FONTS)}")
    lines.append("")
    lines.append("// codepoints extras (slots 224+)")
    lines.append("static const unsigned short g_fontExtraCps[] = {" + ", ".join(f"0x{c:04X}" for c in EXTRA_CPS) + "};")
    lines.append(f"#define FONT_EXTRA_COUNT {len(EXTRA_CPS)}")
    lines.append("")
    names, size = build_icons()
    print(f"icons: {len(names)} ({size} bytes)")
    lines.append("// icones (mascaras alfa)")
    for i, n in enumerate(names):
        lines.append(f"#define {n} {i}")
    lines.append(f"#define IC_COUNT {len(names)}")
    with open(os.path.join(SRC, "assets_gen.h"), "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines) + "\n")
    build_banner_icon()
    font_preview()
    print("ok")


if __name__ == "__main__":
    main()

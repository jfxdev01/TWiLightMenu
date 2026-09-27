# Gera os dados do jogo de corrida do DSi Dash:
#   - pista (spline fechada amostrada), linha de corrida da IA, velocidades-alvo
#   - cenario (palmeiras, predios, arquibancadas, placas, boosts), terreno em celulas
#   - modelo 3D do carro
#   - texturas no formato do hardware 3D do DS (blob unico)
#   python tools/gen_race.py
# Saidas: arm9/source/race_data.h, arm9/data/race_tex.bin, tools/preview_race_*.png
#
# Sistema de coordenadas (igual ao do OpenGL no DS): x = leste, y = cima, z = sul
# (o norte e -z). No PNG de previa, x vai para a direita e z para baixo.
import math
import os
import random
import struct

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "arm9", "source", "race_data.h")
OUT_TEX = os.path.join(ROOT, "arm9", "data", "race_tex.bin")
FONT = os.path.join(ROOT, "assets", "Nunito.ttf")

random.seed(2026)

# ---------------------------------------------------------------------------
# pista
# ---------------------------------------------------------------------------
SEG_LEN = 8.0      # metros entre secoes
HALF_W = 8.0       # meia largura do asfalto
CURB_W = 1.4
RUNOFF = 12.0      # area de escape (areia/grama) alem da zebra

# pontos de controle (x, z): reta da praia no sentido norte (-z), mar a oeste
CTRL = [
    (0, 300), (0, 0), (0, -300),           # reta da praia
    (30, -430), (140, -500),               # curva longa a direita
    (290, -490), (360, -420),              # direita
    (350, -320), (270, -270), (230, -190), # S
    (270, -110), (370, -80), (440, -10),   # esquerda-direita
    (455, 130),                            # reta
    (430, 250), (340, 290),                # direita para oeste
    (250, 250), (200, 310),                # chicane
    (170, 420), (100, 480),
    (20, 470), (0, 400),                   # volta para a reta
]


def catmull(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return tuple(0.5 * ((2 * p1[i]) + (-p0[i] + p2[i]) * t + (2 * p0[i] - 5 * p1[i] + 4 * p2[i] - p3[i]) * t2 + (-p0[i] + 3 * p1[i] - 3 * p2[i] + p3[i]) * t3) for i in range(2))


def build_track():
    n = len(CTRL)
    dense = []
    for i in range(n):
        p0, p1, p2, p3 = CTRL[(i - 1) % n], CTRL[i], CTRL[(i + 1) % n], CTRL[(i + 2) % n]
        for k in range(200):
            dense.append(catmull(p0, p1, p2, p3, k / 200))
    # reamostra por comprimento de arco
    cum = [0.0]
    for i in range(1, len(dense) + 1):
        a, b = dense[i - 1], dense[i % len(dense)]
        cum.append(cum[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = cum[-1]
    nseg = int(round(total / SEG_LEN))
    step = total / nseg
    pts = []
    j = 0
    for s in range(nseg):
        target = s * step
        while cum[j + 1] < target:
            j += 1
        f = (target - cum[j]) / max(1e-9, cum[j + 1] - cum[j])
        a, b = dense[j], dense[(j + 1) % len(dense)]
        pts.append((a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f))
    return pts, step


PTS, STEP = build_track()
N = len(PTS)
TRACK_LEN = N * STEP

# tangente, vetor direita = (-fz, fx), curvatura
TAN, RIGHT, CURV = [], [], []
for i in range(N):
    a, b = PTS[(i - 1) % N], PTS[(i + 1) % N]
    dx, dz = b[0] - a[0], b[1] - a[1]
    l = math.hypot(dx, dz)
    TAN.append((dx / l, dz / l))
    RIGHT.append((-dz / l, dx / l))
for i in range(N):
    t0, t1 = TAN[(i - 1) % N], TAN[(i + 1) % N]
    ang = math.atan2(t0[0] * t1[1] - t0[1] * t1[0], t0[0] * t1[0] + t0[1] * t1[1])
    CURV.append(ang / (2 * STEP))  # >0 = curva para a direita


def smooth(vals, r):
    out = []
    for i in range(len(vals)):
        s = 0
        w = 0
        for k in range(-r, r + 1):
            ww = r + 1 - abs(k)
            s += vals[(i + k) % len(vals)] * ww
            w += ww
        out.append(s / w)
    return out


CURV_S = smooth(CURV, 4)
# linha de corrida: por dentro das curvas (antecipada)
LINE = [max(-HALF_W * 0.62, min(HALF_W * 0.62, c * 900.0)) for c in smooth(CURV, 9)]
LINE = LINE[3:] + LINE[:3]
# velocidade-alvo pela curvatura, com frenagem/aceleracao
A_LAT, A_BRAKE, A_ACC, VMAX = 17.0, 20.0, 9.0, 52.0
VT = [min(VMAX, math.sqrt(A_LAT / max(1e-4, abs(c)))) for c in CURV_S]
for _ in range(3):
    for i in range(N - 1, -1, -1):
        VT[i] = min(VT[i], math.sqrt(VT[(i + 1) % N] ** 2 + 2 * A_BRAKE * STEP))
    for i in range(N):
        VT[i] = min(VT[i], math.sqrt(VT[(i - 1) % N] ** 2 + 2 * A_ACC * STEP * 3))

min_r = min(1 / max(1e-9, abs(c)) for c in CURV_S)
print(f"pista: {N} secoes, {TRACK_LEN:.0f} m, menor raio {min_r:.0f} m")


def dist_to_track(x, z):
    best = 1e9
    for (px, pz) in PTS:
        d = (px - x) ** 2 + (pz - z) ** 2
        if d < best:
            best = d
    return math.sqrt(best)


# lado do mar: tudo com x < COAST e agua
COAST = -70.0


def side_type(i, side):
    # 0 grama, 1 areia (praia)
    x, z = PTS[i]
    rx, rz = RIGHT[i]
    ex = x + rx * side * (HALF_W + 6)
    return 1 if ex < -4 else 0


# zebras onde a curvatura e alta
CURB = [1 if abs(CURV_S[i]) > 1 / 95 else 0 for i in range(N)]
CURB = [1 if any(CURB[(i + k) % N] for k in range(-3, 4)) else 0 for i in range(N)]

# barreiras de pneu no lado de fora de curvas fortes
BARRIER = []
for i in range(N):
    b = 0
    if abs(CURV_S[i]) > 1 / 110:
        b = 2 if CURV_S[i] > 0 else 1  # 1 = lado esquerdo, 2 = direito (fora da curva)
    BARRIER.append(b)
BARRIER = [max(BARRIER[(i + k) % N] for k in range(-2, 3)) for i in range(N)]

# boosts (setas no asfalto): posicao (secao, deslocamento lateral)
BOOSTS = []
for frac, lat in ((0.22, -3.0), (0.55, 3.5), (0.80, 0.0)):
    BOOSTS.append((int(N * frac) % N, lat))

START_SEG = 20  # largada um pouco depois do inicio da reta

# ---------------------------------------------------------------------------
# cenario
# ---------------------------------------------------------------------------
OBJ_PALM, OBJ_BUSH, OBJ_BUILDING, OBJ_STAND, OBJ_AD, OBJ_GANTRY, OBJ_LIGHTHOUSE = range(7)
objs = []  # (tipo, x, z, ang(graus), p1, p2)


def free_spot(x, z, margin):
    return dist_to_track(x, z) > HALF_W + CURB_W + RUNOFF + margin


# palmeiras ao longo da pista
for i in range(0, N, 3):
    for side in (-1, 1):
        if random.random() < 0.45:
            continue
        off = HALF_W + CURB_W + RUNOFF + 3 + random.random() * 14
        x = PTS[i][0] + RIGHT[i][0] * side * off
        z = PTS[i][1] + RIGHT[i][1] * side * off
        if x < COAST + 6:
            continue
        if free_spot(x, z, 2):
            objs.append((OBJ_PALM if (x < 60 or random.random() < 0.6) else OBJ_BUSH, x, z, 0, int(8 + random.random() * 5), 0))
# arbustos espalhados no interior
for _ in range(90):
    x, z = random.uniform(40, 520), random.uniform(-560, 540)
    if free_spot(x, z, 4):
        objs.append((OBJ_BUSH, x, z, 0, int(3 + random.random() * 2), 0))
# cidade: predios a leste
for gx in range(560, 900, 70):
    for gz in range(-620, 640, 70):
        if random.random() < 0.25:
            continue
        x, z = gx + random.uniform(-10, 10), gz + random.uniform(-10, 10)
        w = random.uniform(22, 40)
        h = random.uniform(30, 110) if gx > 620 else random.uniform(20, 55)
        objs.append((OBJ_BUILDING, x, z, 0, int(w), int(h)))
# alguns predios dentro do circuito
for (x, z) in ((250, 30), (330, -200), (150, 150), (200, -380)):
    if free_spot(x, z, 20):
        objs.append((OBJ_BUILDING, x, z, 0, 30, int(random.uniform(25, 45))))
# arquibancadas nos dois lados da largada
for side, dz in ((1, 30), (1, 90), (-1, 60)):
    i = (START_SEG + int(dz / STEP)) % N
    off = HALF_W + CURB_W + RUNOFF + 6
    x = PTS[i][0] + RIGHT[i][0] * side * off
    z = PTS[i][1] + RIGHT[i][1] * side * off
    ang = math.degrees(math.atan2(TAN[i][0], -TAN[i][1]))
    objs.append((OBJ_STAND, x, z, int(ang), side, 0))
# placas de propaganda nas retas
for k, frac in enumerate((0.05, 0.36, 0.48, 0.66, 0.93)):
    i = int(N * frac) % N
    side = 1 if k % 2 == 0 else -1
    if side_type(i, side) == 1:
        side = -side
    off = HALF_W + CURB_W + RUNOFF + 2
    x = PTS[i][0] + RIGHT[i][0] * side * off
    z = PTS[i][1] + RIGHT[i][1] * side * off
    ang = math.degrees(math.atan2(TAN[i][0], -TAN[i][1]))
    objs.append((OBJ_AD, x, z, int(ang), k % 3, side))
# portico de largada
i = START_SEG
objs.append((OBJ_GANTRY, PTS[i][0], PTS[i][1], int(math.degrees(math.atan2(TAN[i][0], -TAN[i][1]))), 0, 0))
# farol na praia
objs.append((OBJ_LIGHTHOUSE, -48, -470, 0, 0, 0))

print(f"cenario: {len(objs)} objetos")

# terreno em celulas de 64 m: 0 grama, 1 areia, 2 agua, 3 cidade
CELL = 64
GX0, GZ0 = -640, -768
GW, GH = 26, 24
cells = []
for gz in range(GH):
    row = []
    for gx in range(GW):
        x0 = GX0 + gx * CELL
        cx = x0 + CELL / 2
        if x0 + CELL <= COAST:
            t = 2
        elif x0 < 0:
            t = 1
        elif x0 >= 560:
            t = 3
        else:
            t = 0
        row.append(t)
    cells.append(row)

# ---------------------------------------------------------------------------
# previa da pista
# ---------------------------------------------------------------------------
def preview_track():
    S = 0.8
    W, H = int(GW * CELL * S), int(GH * CELL * S)
    im = Image.new("RGB", (W, H), (80, 150, 70))
    d = ImageDraw.Draw(im)
    cols = {0: (96, 160, 72), 1: (230, 210, 150), 2: (40, 110, 190), 3: (150, 150, 150)}
    for gz in range(GH):
        for gx in range(GW):
            x0, z0 = gx * CELL * S, gz * CELL * S
            d.rectangle([x0, z0, x0 + CELL * S, z0 + CELL * S], fill=cols[cells[gz][gx]])

    def P(x, z):
        return ((x - GX0) * S, (z - GZ0) * S)

    for i in range(N):
        a, b = PTS[i], PTS[(i + 1) % N]
        w = (HALF_W * 2) * S
        d.line([P(*a), P(*b)], fill=(60, 60, 64) if not CURB[i] else (200, 40, 40), width=int(w))
    for i in range(N):
        a = PTS[i]
        lx = a[0] + RIGHT[i][0] * LINE[i]
        lz = a[1] + RIGHT[i][1] * LINE[i]
        b = PTS[(i + 1) % N]
        lx2 = b[0] + RIGHT[(i + 1) % N][0] * LINE[(i + 1) % N]
        lz2 = b[1] + RIGHT[(i + 1) % N][1] * LINE[(i + 1) % N]
        v = VT[i] / VMAX
        d.line([P(lx, lz), P(lx2, lz2)], fill=(int(255 * (1 - v)), int(255 * v), 0), width=2)
    colo = {OBJ_PALM: (20, 90, 20), OBJ_BUSH: (40, 120, 40), OBJ_BUILDING: (90, 90, 110), OBJ_STAND: (200, 200, 60), OBJ_AD: (250, 120, 0), OBJ_GANTRY: (255, 255, 255), OBJ_LIGHTHOUSE: (255, 60, 60)}
    for o in objs:
        x, y = P(o[1], o[2])
        r = 3 if o[0] != OBJ_BUILDING else o[4] * S / 2
        d.rectangle([x - r, y - r, x + r, y + r], fill=colo[o[0]])
    for (si, lat) in BOOSTS:
        x, y = P(PTS[si][0] + RIGHT[si][0] * lat, PTS[si][1] + RIGHT[si][1] * lat)
        d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=(255, 220, 0))
    x, y = P(*PTS[START_SEG])
    d.ellipse([x - 7, y - 7, x + 7, y + 7], outline=(255, 255, 255), width=3)
    im.save(os.path.join(ROOT, "tools", "preview_race_track.png"))


# ---------------------------------------------------------------------------
# texturas
# ---------------------------------------------------------------------------
def rgb555(r, g, b, a=1):
    return (int(r) >> 3) | ((int(g) >> 3) << 5) | ((int(b) >> 3) << 10) | (0x8000 if a else 0)


def noise_img(w, h, base, amp, scale=1, seed=0):
    rnd = random.Random(seed)
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            n = rnd.uniform(-amp, amp)
            px[x, y] = tuple(max(0, min(255, int(c + n))) for c in base)
    if scale > 1:
        im = im.filter(ImageFilter.GaussianBlur(scale))
    return im


def tile_blur(im, r):
    # desfoque que respeita a repeticao da textura
    w, h = im.size
    big = Image.new("RGB", (w * 3, h * 3))
    for i in range(3):
        for j in range(3):
            big.paste(im, (i * w, j * h))
    big = big.filter(ImageFilter.GaussianBlur(r))
    return big.crop((w, h, 2 * w, 2 * h))


def tex_road():
    w, h = 128, 128
    im = noise_img(w, h, (88, 88, 94), 38, 0, 1)
    im = tile_blur(im, 0.7)
    fine = noise_img(w, h, (0, 0, 0), 14, 0, 2)
    px, fp = im.load(), fine.load()
    for y in range(h):
        for x in range(w):
            p = px[x, y]
            f = fp[x, y][0] - 7
            px[x, y] = (p[0] + f, p[1] + f, p[2] + f)
    d = ImageDraw.Draw(im)
    # marcas de pneu escuras nas trilhas
    for cx in (34, 94):
        for x in range(cx - 7, cx + 8):
            for y in range(h):
                p = px[x % w, y]
                k = 0.86 + 0.06 * math.cos((x - cx) / 7 * math.pi)
                px[x % w, y] = tuple(int(c * k) for c in p)
    # faixas laterais brancas e central tracejada
    d.rectangle([3, 0, 5, h], fill=(236, 236, 230))
    d.rectangle([w - 6, 0, w - 4, h], fill=(236, 236, 230))
    d.rectangle([63, 0, 64, 60], fill=(240, 240, 234))
    return im


def tex_grass():
    w, h = 128, 128
    im = noise_img(w, h, (92, 150, 62), 26, 0, 3)
    im = tile_blur(im, 1.0)
    px = im.load()
    rnd = random.Random(4)
    for y in range(h):
        for x in range(w):
            p = px[x, y]
            k = 1.0 + (0.07 if (x // 32) % 2 == 0 else -0.05)  # faixas de corte
            n = rnd.uniform(-12, 12)
            px[x, y] = (int(p[0] * k + n), int(p[1] * k + n), int(p[2] * k + n * 0.5))
    return im


def tex_sand():
    im = noise_img(64, 64, (224, 204, 150), 26, 0, 5)
    return tile_blur(im, 0.6)


def tex_water():
    w, h = 64, 64
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            v = math.sin(x / w * 2 * math.pi * 2 + math.sin(y / h * 2 * math.pi) * 1.5) + math.sin(y / h * 2 * math.pi * 3 + x / w * 2 * math.pi)
            b = 0.5 + 0.25 * v
            px[x, y] = (int(20 + 30 * b), int(110 + 50 * b), int(170 + 60 * b))
    im = tile_blur(im, 0.8)
    d = ImageDraw.Draw(im)
    rnd = random.Random(6)
    for _ in range(14):
        x, y = rnd.randrange(w), rnd.randrange(h)
        d.line([(x, y), (x + rnd.randrange(3, 7), y)], fill=(210, 240, 255))
    return im


def tex_city():
    im = noise_img(64, 64, (150, 150, 146), 18, 0, 7)
    d = ImageDraw.Draw(im)
    for k in (0, 32):
        d.line([(k, 0), (k, 64)], fill=(120, 120, 118))
        d.line([(0, k), (64, k)], fill=(120, 120, 118))
    return tile_blur(im, 0.4)


def tex_curb():
    im = Image.new("RGB", (8, 32))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 8, 15], fill=(214, 36, 36))
    d.rectangle([0, 16, 8, 32], fill=(242, 242, 242))
    return im


def tex_checker():
    im = Image.new("RGB", (32, 32))
    d = ImageDraw.Draw(im)
    for y in range(4):
        for x in range(4):
            d.rectangle([x * 8, y * 8, x * 8 + 7, y * 8 + 7], fill=(20, 20, 20) if (x + y) % 2 else (245, 245, 245))
    return im


def palm_rgba():
    S = 4
    w, h = 64 * S, 128 * S
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # tronco curvo
    for k in range(60):
        t = k / 59
        x = w / 2 + math.sin(t * 1.3) * 12 * S - 6 * S
        y = h - t * h * 0.66
        r = (4.2 - 1.6 * t) * S
        c = (120 - int(30 * (k % 6 < 3)), 84, 50)
        d.ellipse([x - r, y - r, x + r, y + r], fill=c + (255,))
    top = (w / 2 + math.sin(1.3) * 12 * S - 6 * S, h - h * 0.66)
    # folhas
    for k in range(9):
        a = -math.pi / 2 + (k - 4) * 0.42 + (0.2 if k % 2 else 0)
        L = (24 + (k % 3) * 3) * S
        pts = []
        for j in range(12):
            t = j / 11
            droop = t * t * 16 * S * (1 if abs(math.cos(a)) > 0.3 else 0.4)
            x = top[0] + math.cos(a) * L * t
            y = top[1] + math.sin(a) * L * t * 0.6 + droop
            pts.append((x, y))
        for j in range(len(pts) - 1):
            t = j / 11
            wid = (6 - 5 * t) * S
            g = (36 + int(30 * (1 - t)), 110 + int(40 * (1 - t)), 38)
            d.line([pts[j], pts[j + 1]], fill=g + (255,), width=int(wid))
    for k in range(3):
        d.ellipse([top[0] - 4 * S + k * 3 * S, top[1] - 1 * S, top[0] + k * 3 * S, top[1] + 4 * S], fill=(90, 60, 30, 255))
    return im.resize((64, 128), Image.LANCZOS)


def bush_rgba():
    S = 4
    im = Image.new("RGBA", (32 * S, 32 * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    rnd = random.Random(9)
    for _ in range(22):
        x, y = rnd.uniform(6, 26) * S, rnd.uniform(10, 28) * S
        r = rnd.uniform(4, 8) * S
        g = rnd.randint(90, 150)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(40, g, 40, 255))
    return im.resize((32, 32), Image.LANCZOS)


def mountains_rgba():
    w, h = 256, 64
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    rnd = random.Random(11)
    # 256 px = 360 graus. mar a oeste (u 160..240 sem morros)
    ph = [rnd.uniform(0, 6.28) for _ in range(4)]
    for x in range(w):
        a = x / w * 2 * math.pi
        hh = 26 + 10 * math.sin(a * 2 + ph[0]) + 7 * math.sin(a * 5 + ph[1]) + 4 * math.sin(a * 11 + ph[2]) + 2 * math.sin(a * 23 + ph[3])
        fade = 1.0
        if 150 <= x <= 250:
            fade = max(0.0, abs(x - 200) / 50.0) ** 2
        hh = hh * fade
        for y in range(h):
            yy = h - 1 - y
            if yy < hh:
                t = yy / max(1, hh)
                c = (int(92 + 40 * t), int(130 + 30 * t), int(118 + 40 * t))
                px[x, y] = c + (255,)
    # predios distantes (skyline) no leste (u ~ 40..110)
    d = ImageDraw.Draw(im)
    for k in range(16):
        x = 40 + k * 4.5 + rnd.uniform(-1, 1)
        bh = rnd.uniform(14, 38)
        col = (int(150 + rnd.uniform(-15, 15)), int(162 + rnd.uniform(-15, 15)), 178)
        d.rectangle([x, h - bh, x + rnd.uniform(3, 5), h], fill=col + (255,))
    return im


def soft_alpha(w, h, fn):
    im = Image.new("L", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = int(max(0, min(1, fn((x + 0.5) / w, (y + 0.5) / h))) * 255)
    return im


def tex_building():
    w, h = 32, 64
    im = Image.new("RGB", (w, h), (200, 200, 205))
    d = ImageDraw.Draw(im)
    for y in range(4, h, 8):
        for x in range(3, w, 8):
            lit = random.random() < 0.25
            d.rectangle([x, y, x + 4, y + 4], fill=(250, 230, 150) if lit else (70, 100, 140))
    return im


def tex_crowd():
    w, h = 64, 32
    im = Image.new("RGB", (w, h), (70, 70, 80))
    d = ImageDraw.Draw(im)
    rnd = random.Random(13)
    for y in range(2, h, 5):
        d.line([(0, y + 3), (w, y + 3)], fill=(150, 150, 160))
        for x in range(1, w, 3):
            c = rnd.choice([(230, 60, 60), (60, 120, 230), (250, 220, 60), (240, 240, 240), (60, 180, 90), (240, 140, 40)])
            d.rectangle([x, y, x + 1, y + 2], fill=c)
    return im


def tex_ad(k):
    w, h = 128, 32
    bg = [(0, 180, 220), (230, 40, 60), (250, 190, 30)][k]
    fg = [(255, 255, 255), (255, 255, 255), (40, 40, 40)][k]
    txt = ["DSi Dash", "Maceió 2026", "Wi-Fi 2026"][k]
    S = 4
    im = Image.new("RGB", (w * S, h * S), bg)
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(FONT, 20 * S)
    try:
        f.set_variation_by_axes([900])
    except Exception:
        pass
    bb = d.textbbox((0, 0), txt, font=f)
    d.text(((w * S - (bb[2] - bb[0])) / 2 - bb[0], (h * S - (bb[3] - bb[1])) / 2 - bb[1]), txt, font=f, fill=fg)
    d.rectangle([0, 0, w * S - 1, h * S - 1], outline=(30, 30, 30), width=2 * S)
    return im.resize((w, h), Image.LANCZOS)


def tex_banner():
    w, h = 128, 32
    S = 4
    im = Image.new("RGB", (w * S, h * S), (20, 20, 24))
    d = ImageDraw.Draw(im)
    for x in range(0, w * S, 8 * S):
        for y in (0, (h - 8) * S):
            for k in range(0, 8 * S, 4 * S):
                pass
    for k in range(0, w, 8):
        for r in range(2):
            c = (245, 245, 245) if ((k // 8) + r) % 2 else (20, 20, 20)
            d.rectangle([k * S, r * 4 * S, (k + 8) * S, (r + 1) * 4 * S], fill=c)
            d.rectangle([k * S, (h - 8 + r * 4) * S, (k + 8) * S, (h - 4 + r * 4) * S], fill=c)
    f = ImageFont.truetype(FONT, 14 * S)
    try:
        f.set_variation_by_axes([900])
    except Exception:
        pass
    txt = "LARGADA"
    bb = d.textbbox((0, 0), txt, font=f)
    d.text(((w * S - (bb[2] - bb[0])) / 2 - bb[0], (h * S - (bb[3] - bb[1])) / 2 - bb[1]), txt, font=f, fill=(0, 200, 235))
    return im.resize((w, h), Image.LANCZOS)


def tex_arrow_rgba():
    S = 4
    im = Image.new("RGBA", (32 * S, 32 * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for k, y0 in enumerate((2, 16)):
        c = (255, 200 - k * 40, 0, 255)
        d.polygon([(4 * S, (y0 + 12) * S), (16 * S, y0 * S), (28 * S, (y0 + 12) * S), (22 * S, (y0 + 12) * S), (16 * S, (y0 + 6) * S), (10 * S, (y0 + 12) * S)], fill=c)
    return im.resize((32, 32), Image.LANCZOS)


def tex_tires():
    im = Image.new("RGB", (32, 16), (30, 30, 34))
    d = ImageDraw.Draw(im)
    for x in range(0, 32, 8):
        d.ellipse([x, 1, x + 7, 7], fill=(50, 50, 55), outline=(18, 18, 20))
        d.ellipse([x + 4, 8, x + 11, 14], fill=(50, 50, 55), outline=(18, 18, 20))
        d.rectangle([x, 7, x + 7, 8], fill=(220, 40, 40) if (x // 8) % 2 else (240, 240, 240))
    return im


def tex_rail():
    im = Image.new("RGB", (32, 8), (170, 176, 184))
    d = ImageDraw.Draw(im)
    d.line([(0, 2), (32, 2)], fill=(220, 225, 232))
    d.line([(0, 5), (32, 5)], fill=(120, 124, 132))
    return im


def tex_stone():
    im = noise_img(32, 32, (190, 184, 170), 24, 0, 17)
    d = ImageDraw.Draw(im)
    for y in range(0, 32, 8):
        d.line([(0, y), (32, y)], fill=(150, 144, 132))
    return tile_blur(im, 0.3)


# formatos: 'rgb' (direta), 'rgba' (direta com alfa de 1 bit), 'a5' (8 cores + alfa 5 bits)
TEXTURES = []


def add_rgb(name, im, fmt="rgb"):
    TEXTURES.append((name, fmt, im))


def add_a5(name, alpha, color):
    TEXTURES.append((name, "a5", (alpha, color)))


def build_textures():
    add_rgb("road", tex_road())
    add_rgb("grass", tex_grass())
    add_rgb("sand", tex_sand())
    add_rgb("water", tex_water())
    add_rgb("city", tex_city())
    add_rgb("curb", tex_curb())
    add_rgb("checker", tex_checker())
    add_rgb("palm", palm_rgba(), "rgba")
    add_rgb("bush", bush_rgba(), "rgba")
    add_rgb("mount", mountains_rgba(), "rgba")
    add_rgb("building", tex_building())
    add_rgb("crowd", tex_crowd())
    add_rgb("ad0", tex_ad(0))
    add_rgb("ad1", tex_ad(1))
    add_rgb("ad2", tex_ad(2))
    add_rgb("banner", tex_banner())
    add_rgb("arrow", tex_arrow_rgba(), "rgba")
    add_rgb("tires", tex_tires())
    add_rgb("rail", tex_rail())
    add_rgb("stone", tex_stone())
    add_a5("shadow", soft_alpha(32, 32, lambda u, v: 0.85 * max(0, 1 - ((u - .5) ** 2 / .22 + (v - .5) ** 2 / .22) ** 1.2)), (0, 0, 0))
    add_a5("glow", soft_alpha(32, 32, lambda u, v: max(0, 1 - math.hypot(u - .5, v - .5) * 2) ** 1.6), (255, 255, 255))
    add_a5("cloud", soft_alpha(64, 32, lambda u, v: max(0, 1 - ((u - .5) ** 2 / .2 + (v - .6) ** 2 / .09)) ** 0.8 * (0.6 + 0.4 * math.sin(u * 17) * math.sin(v * 9 + 1) ** 2)), (255, 255, 255))
    add_a5("spark", soft_alpha(16, 16, lambda u, v: max(0, 1 - math.hypot(u - .5, v - .5) * 2)), (255, 255, 255))


SIZES = {8: 0, 16: 1, 32: 2, 64: 3, 128: 4, 256: 5, 512: 6, 1024: 7}


def encode_textures():
    blob = bytearray()
    meta = []
    sheet = []
    for name, fmt, data in TEXTURES:
        off = len(blob)
        pal_off = -1
        if fmt in ("rgb", "rgba"):
            im = data.convert("RGBA")
            w, h = im.size
            for y in range(h):
                for x in range(w):
                    r, g, b, a = im.getpixel((x, y))
                    blob += struct.pack("<H", rgb555(r, g, b, a >= 128 if fmt == "rgba" else 1))
            sheet.append(im)
        else:
            alpha, color = data
            w, h = alpha.size
            for y in range(h):
                for x in range(w):
                    a5 = alpha.getpixel((x, y)) >> 3
                    blob.append((a5 << 3) | 0)
            while len(blob) % 4:
                blob.append(0)
            pal_off = len(blob)
            pal = [rgb555(*color, 0)] + [0] * 7
            for c in pal:
                blob += struct.pack("<H", c & 0x7FFF)
            prev = Image.new("RGBA", (w, h), color + (0,))
            prev.putalpha(alpha)
            sheet.append(prev)
        while len(blob) % 4:
            blob.append(0)
        ftype = {"rgb": 8, "rgba": 7, "a5": 6}[fmt]
        meta.append((name, off, pal_off, w, h, SIZES[w], SIZES[h], ftype))
    with open(OUT_TEX, "wb") as f:
        f.write(blob)
    # folha de previa
    W = 700
    x = y = 0
    rowh = 0
    pv = Image.new("RGB", (W, 700), (255, 0, 255))
    for im in sheet:
        s = im.convert("RGBA")
        if s.width < 64:
            s = s.resize((s.width * 2, s.height * 2), Image.NEAREST)
        if x + s.width > W:
            x = 0
            y += rowh + 6
            rowh = 0
        bgc = Image.new("RGBA", s.size, (60, 60, 60, 255))
        bgc.alpha_composite(s)
        pv.paste(bgc.convert("RGB"), (x, y))
        x += s.width + 6
        rowh = max(rowh, s.height)
    pv.crop((0, 0, W, y + rowh + 4)).save(os.path.join(ROOT, "tools", "preview_race_textures.png"))
    print(f"texturas: {len(meta)} ({len(blob) // 1024} KB)")
    return meta


# ---------------------------------------------------------------------------
# carro (x direita, y cima, frente para -z)
# ---------------------------------------------------------------------------
MAT_BODY, MAT_GLASS, MAT_TRIM, MAT_HEAD, MAT_TAIL, MAT_TIRE, MAT_RIM = range(7)
POLYS = []  # (material, [(x,y,z)...], normal por vertice ou None)


def v_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def v_norm(a):
    l = math.sqrt(sum(c * c for c in a)) or 1
    return tuple(c / l for c in a)


def poly(mat, pts, smooth_n=None):
    # pts em ordem anti-horaria vista de fora
    POLYS.append((mat, pts, smooth_n))


def build_car():
    # carroceria por secoes transversais (loft), frente em -z
    # (z, w lateral, y base, y ombro, y topo, w topo, cabine)
    ST = [
        (-2.18, 0.80, 0.34, 0.50, 0.58, 0.60, 0),
        (-1.92, 0.93, 0.28, 0.60, 0.72, 0.78, 0),
        (-0.95, 0.95, 0.27, 0.64, 0.83, 0.86, 0),
        (-0.22, 0.95, 0.27, 0.66, 1.14, 0.60, 1),
        (0.72, 0.95, 0.27, 0.66, 1.12, 0.62, 1),
        (1.52, 0.95, 0.27, 0.66, 0.90, 0.84, 0),
        (2.12, 0.90, 0.30, 0.62, 0.84, 0.76, 0),
    ]

    def ring(st, side):
        z, w, yb, ys, yt, wt, cab = st
        belt = min(yt, ys + 0.1)
        return [(side * w, yb, z), (side * w, ys, z), (side * w * 0.95, belt, z), (side * wt, yt, z)]

    for k in range(len(ST) - 1):
        a, b = ST[k], ST[k + 1]
        glass_top = (k == 2) or (k == 4)          # para-brisa e vidro traseiro
        glass_side = k in (2, 3, 4)               # janelas laterais
        for side in (-1, 1):
            ra, rb = ring(a, side), ring(b, side)
            for s in range(3):
                mat = MAT_BODY
                if s == 2 and glass_side:
                    mat = MAT_GLASS
                if s == 0 and (k == 0 or k == 5):
                    mat = MAT_BODY
                poly(mat, [ra[s], rb[s], rb[s + 1], ra[s + 1]], "body" if mat == MAT_BODY else "glass")
            # topo: da borda ate o centro
            ca = (0.0, a[4] + 0.02, a[0])
            cb = (0.0, b[4] + 0.02, b[0])
            mat = MAT_GLASS if glass_top else MAT_BODY
            poly(mat, [ra[3], rb[3], cb, ca], "body" if mat == MAT_BODY else "glass")
    # frente (grade + farois) e traseira (lanternas)
    for idx, end in ((0, -1), (len(ST) - 1, 1)):
        st = ST[idx]
        L, Rr = ring(st, -1), ring(st, 1)
        c = (0.0, st[4] + 0.02, st[0])
        poly(MAT_TRIM, [L[0], Rr[0], Rr[1], L[1]], None)
        poly(MAT_BODY, [L[1], Rr[1], Rr[2], L[2]], None)
        poly(MAT_BODY, [L[2], Rr[2], Rr[3], L[3]], None)
        poly(MAT_BODY, [L[3], Rr[3], c, c], None)
        z = st[0] + end * 0.012
        for side in (-1, 1):
            x0, x1 = side * 0.78, side * 0.42
            if end < 0:
                y0, y1 = 0.46, 0.56
                poly(MAT_HEAD, [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], None)
            else:
                y0, y1 = 0.56, 0.68
                poly(MAT_TAIL, [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], None)
    # aerofolio
    poly(MAT_TRIM, [(-0.88, 0.98, 1.72), (0.88, 0.98, 1.72), (0.88, 1.0, 2.08), (-0.88, 1.0, 2.08)], None)
    for x in (-0.66, 0.62):
        poly(MAT_TRIM, [(x, 0.86, 1.9), (x + 0.04, 0.86, 1.9), (x + 0.04, 0.99, 1.9), (x, 0.99, 1.9)], None)
    # rodas (hexagonais)
    R, Wd = 0.35, 0.27
    for wx in (-0.84, 0.84):
        for wz in (-1.32, 1.34):
            rng = [(wz + R * math.cos(t), R + R * math.sin(t)) for t in [k * math.pi / 3 + math.pi / 6 for k in range(6)]]
            xo = wx + math.copysign(Wd / 2, wx)
            xi = wx - math.copysign(Wd / 2, wx)
            for k in range(6):
                z0, y0 = rng[k]
                z1, y1 = rng[(k + 1) % 6]
                poly(MAT_TIRE, [(xo, y0, z0), (xo, y1, z1), (xi, y1, z1), (xi, y0, z0)], None)
            cap = [(xo, y, z) for (z, y) in rng]
            poly(MAT_RIM, [cap[0], cap[1], cap[2], cap[3]], None)
            poly(MAT_RIM, [cap[3], cap[4], cap[5], cap[0]], None)


def car_normals():
    # orienta as faces para fora, ordena por material e calcula normais por vertice
    # (suavizadas dentro do mesmo grupo quando o angulo entre faces e pequeno)
    fixed = []
    for mat, pts, grp in POLYS:
        n = v_norm(v_cross(v_sub(pts[1], pts[0]), v_sub(pts[2] if pts[2] != pts[1] else pts[3], pts[0])))
        c = tuple(sum(p[k] for p in pts) / len(pts) for k in range(3))
        center = (0.0, 0.62, 0.0)
        if mat in (MAT_TIRE, MAT_RIM):
            center = (math.copysign(0.84, c[0]) if mat == MAT_TIRE else 0.0, 0.35, math.copysign(1.33, c[2]))
        if mat in (MAT_HEAD, MAT_TAIL):
            center = (c[0], c[1], 0.0)
        if sum(a * (b - cc) for a, b, cc in zip(n, c, center)) < 0:
            pts = pts[::-1]
            n = tuple(-v for v in n)
        fixed.append((mat, pts, grp, n))
    fixed.sort(key=lambda f: f[0])
    acc = {}
    for mat, pts, grp, n in fixed:
        if not grp:
            continue
        for p in pts:
            key = (round(p[0], 3), round(p[1], 3), round(p[2], 3), grp)
            acc.setdefault(key, []).append(n)
    POLYS[:] = []
    out = []
    for mat, pts, grp, n in fixed:
        vn = []
        for p in pts:
            if grp:
                key = (round(p[0], 3), round(p[1], 3), round(p[2], 3), grp)
                sel = [m for m in acc[key] if sum(a * b for a, b in zip(m, n)) > 0.55]
                s = [sum(m[k] for m in sel) for k in range(3)]
                vn.append(v_norm(s))
            else:
                vn.append(n)
        POLYS.append((mat, pts, grp))
        out.append((n, vn))
    return out


def preview_car(normals):
    W, H = 600, 300
    im = Image.new("RGB", (W, H), (200, 220, 240))
    d = ImageDraw.Draw(im)
    light = v_norm((-0.42, 0.78, -0.46))
    cols = {MAT_BODY: (220, 30, 40), MAT_GLASS: (30, 40, 60), MAT_TRIM: (30, 30, 34), MAT_HEAD: (255, 250, 210), MAT_TAIL: (255, 40, 30), MAT_TIRE: (25, 25, 25), MAT_RIM: (190, 190, 200)}
    for view, yawd in ((0, 150), (1, 35)):
        yaw = math.radians(yawd)
        cy, sy = math.cos(yaw), math.sin(yaw)

        def tr(p):
            x, y, z = p
            return (x * cy - z * sy, y, x * sy + z * cy)

        polys = []
        for (mat, pts, _), (n, vn) in zip(POLYS, normals):
            tp = [tr(p) for p in pts]
            tn = tr(n)
            if tn[2] > 0.05:  # olhando de +z para -z: descarta faces viradas para longe
                continue
            depth = sum(p[2] for p in tp) / len(tp)
            lit = max(0.3, sum(a * b for a, b in zip(v_norm(tuple(sum(v[k] for v in vn) for k in range(3))), light)))
            if mat in (MAT_HEAD, MAT_TAIL):
                lit = 1
            polys.append((depth, [(150 + view * 300 + p[0] * 55, 200 - p[1] * 55 + p[2] * 10) for p in tp], tuple(int(v * lit) for v in cols[mat])))
        for _, pts, c in sorted(polys, key=lambda t: t[0]):
            d.polygon(pts, fill=c, outline=(0, 0, 0))
    im.save(os.path.join(ROOT, "tools", "preview_race_car.png"))


# ---------------------------------------------------------------------------
# saida C
# ---------------------------------------------------------------------------
def f32(v):
    return int(round(v * 4096))


def main():
    build_textures()
    meta = encode_textures()
    build_car()
    normals = car_normals()
    preview_car(normals)
    preview_track()
    L = ["// gerado por tools/gen_race.py -- nao editar", "#pragma once", "#include <nds.h>", ""]

    def array(decl, rows):
        # declaracao extern (sempre) + definicao (so em RACE_DATA_IMPL)
        L.append(f"extern const {decl};")
        L.append("#ifdef RACE_DATA_IMPL")
        L.append(f"const {decl} = {{")
        L.extend(rows)
        L.append("};")
        L.append("#endif")
        L.append("")

    L.append(f"#define RT_N {N}")
    L.append(f"#define RT_SEG_LEN_F32 {f32(STEP)}")
    L.append(f"#define RT_LEN_F32 {f32(TRACK_LEN)}")
    L.append(f"#define RT_SEG_LEN {STEP:.4f}f")
    L.append(f"#define RT_LEN {TRACK_LEN:.3f}f")
    L.append(f"#define RT_HALF_W {HALF_W}f")
    L.append(f"#define RT_CURB_W {CURB_W}f")
    L.append(f"#define RT_RUNOFF {RUNOFF}f")
    L.append(f"#define RT_START_SEG {START_SEG}")
    L.append(f"#define RT_COAST {COAST}f")
    L.append("")
    L.append("typedef struct RtSec { s32 x, z; s16 rx, rz; s16 tx, tz; u8 flags, vt; s8 line, curv; } RtSec;")
    L.append("// flags: bit0 zebra, bit1 barreira esq, bit2 barreira dir, bit3 areia esq, bit4 areia dir")
    L.append("// vt = velocidade-alvo * 4 (m/s); line = deslocamento da linha de corrida * 10 (m)")
    rows = []
    for i in range(N):
        fl = CURB[i] | ((1 if BARRIER[i] == 1 else 0) << 1) | ((1 if BARRIER[i] == 2 else 0) << 2) | (side_type(i, -1) << 3) | (side_type(i, 1) << 4)
        rows.append(f"\t{{{f32(PTS[i][0])}, {f32(PTS[i][1])}, {f32(RIGHT[i][0])}, {f32(RIGHT[i][1])}, {f32(TAN[i][0])}, {f32(TAN[i][1])}, {fl}, {min(255, int(VT[i] * 4))}, {int(round(LINE[i] * 10))}, {max(-127, min(127, int(CURV_S[i] * 3000)))}}},")
    array("RtSec g_rt[RT_N]", rows)

    L.append(f"#define RT_NBOOST {len(BOOSTS)}")
    array("s16 g_rtBoost[RT_NBOOST][2]", ["\t" + ", ".join(f"{{{s}, {int(lat * 10)}}}" for s, lat in BOOSTS)])

    L.append("enum { RO_PALM, RO_BUSH, RO_BUILDING, RO_STAND, RO_AD, RO_GANTRY, RO_LIGHTHOUSE };")
    L.append("typedef struct RtObj { u8 type; s16 x, z, ang; s16 p1, p2; } RtObj;")
    L.append(f"#define RT_NOBJ {len(objs)}")
    array("RtObj g_rtObj[RT_NOBJ]", [f"\t{{{o[0]}, {int(round(o[1]))}, {int(round(o[2]))}, {o[3]}, {o[4]}, {o[5]}}}," for o in objs])

    L.append(f"#define RT_CELL {CELL}")
    L.append(f"#define RT_GX0 {GX0}")
    L.append(f"#define RT_GZ0 {GZ0}")
    L.append(f"#define RT_GW {GW}")
    L.append(f"#define RT_GH {GH}")
    L.append("// 0 grama, 1 areia, 2 agua, 3 cidade")
    array("u8 g_rtCells[RT_GH][RT_GW]", ["\t{" + ", ".join(str(c) for c in row) + "}," for row in cells])

    L.append("// texturas: offset, offset da paleta (-1 = sem), w, h, tamanho x/y (enum), tipo GL")
    L.append("typedef struct RtTex { s32 off, pal; u16 w, h; u8 sx, sy, type; } RtTex;")
    L.append("enum { " + ", ".join(f"TX_{m[0].upper()}" for m in meta) + ", TX_COUNT };")
    array("RtTex g_rtTex[TX_COUNT]", [f"\t{{{m[1]}, {m[2]}, {m[3]}, {m[4]}, {m[5]}, {m[6]}, {m[7]}}},  // {m[0]}" for m in meta])

    L.append("enum { MAT_BODY, MAT_GLASS, MAT_TRIM, MAT_HEAD, MAT_TAIL, MAT_TIRE, MAT_RIM };")
    L.append("typedef struct CarPoly { u8 mat, n; u32 normal[4]; s16 v[4][3]; } CarPoly;")
    L.append(f"#define CAR_NPOLY {len(POLYS)}")

    def v10(c):
        v = int(round(c * 511))
        v = max(-512, min(511, v))
        return v & 0x3FF

    rows = []
    for (mat, pts, _), (n, vn) in zip(POLYS, normals):
        pts = list(pts)
        vn = list(vn)
        while len(pts) < 4:
            pts.append(pts[-1])
            vn.append(vn[-1])
        ns = ", ".join(f"0x{v10(q[0]) | (v10(q[1]) << 10) | (v10(q[2]) << 20):08X}" for q in vn)
        vs = ", ".join("{" + ", ".join(str(f32(c)) for c in p) + "}" for p in pts)
        rows.append(f"	{{{mat}, {len(pts)}, {{{ns}}}, {{{vs}}}}},")
    array("CarPoly g_carPoly[CAR_NPOLY]", rows)
    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    print(f"carro: {len(POLYS)} poligonos")
    print("ok")


if __name__ == "__main__":
    main()

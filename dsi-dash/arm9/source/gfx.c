// DSi Dash — renderizacao por software (RGB555) + texto + icones
#include "common.h"
#include "font_small_bin.h"
#include "font_body_bin.h"
#include "font_title_bin.h"
#include "font_big_bin.h"
#include "font_huge_bin.h"
#include "font_text_bin.h"
#include "font_textb_bin.h"
#include "font_h1_bin.h"
#include "font_h2_bin.h"
#include "icons_bin.h"

Canvas g_top, g_bot;
static u16 s_fbTop[SCR_W * SCR_H] __attribute__((aligned(32)));
static u16 s_fbBot[SCR_W * SCR_H] __attribute__((aligned(32)));
static int s_redraw = GFX_BOTH;  // precisa redesenhar
static int s_present;            // desenhado, falta copiar para a VRAM

// ---------------------------------------------------------------------------
// fontes / icones
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) GlyphRec {
	u32 off;
	u8 w, h;
	s8 xo, yo;
	u8 adv, present;
	u16 pad;
} GlyphRec;

typedef struct Font {
	const GlyphRec* g;
	const u8* bits;
	int count, lineH, ascent;
} Font;

static Font s_fonts[FONT_COUNT];
static const u8 s_a4[16] = {0, 2, 4, 6, 9, 11, 13, 15, 17, 19, 21, 23, 26, 28, 30, 32};

typedef struct IconRef {
	const u8* a;
	u16 w, h;
} IconRef;
static IconRef s_icons[IC_COUNT];

static void fontLoad(Font* f, const u8* data) {
	f->lineH = data[4];
	f->ascent = data[5];
	f->count = data[6] | (data[7] << 8);
	f->g = (const GlyphRec*)(data + 12);
	f->bits = data + 12 + f->count * sizeof(GlyphRec);
}

static void iconsLoad(void) {
	const u8* d = icons_bin;
	u32 n = d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24);
	const u8* tab = d + 8;
	const u8* blob = tab + n * 8;
	for (u32 i = 0; i < n && i < IC_COUNT; i++) {
		const u8* e = tab + i * 8;
		u32 off = e[0] | (e[1] << 8) | (e[2] << 16) | (e[3] << 24);
		s_icons[i].a = blob + off;
		s_icons[i].w = e[4] | (e[5] << 8);
		s_icons[i].h = e[6] | (e[7] << 8);
	}
}

// ---------------------------------------------------------------------------
// init / apresentacao
// ---------------------------------------------------------------------------
void gfxInit(void) {
	videoSetMode(MODE_5_2D);
	videoSetModeSub(MODE_5_2D);
	vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
	vramSetBankC(VRAM_C_SUB_BG_0x06200000);
	bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	bgUpdate();
	lcdMainOnBottom();  // engine principal (buffer duplo) na tela de toque

	g_top.px = s_fbTop;
	g_top.which = GFX_TOP;
	g_bot.px = s_fbBot;
	g_bot.which = GFX_BOT;
	cvResetClip(&g_top);
	cvResetClip(&g_bot);

	fontLoad(&s_fonts[FONT_SMALL], font_small_bin);
	fontLoad(&s_fonts[FONT_BODY], font_body_bin);
	fontLoad(&s_fonts[FONT_TITLE], font_title_bin);
	fontLoad(&s_fonts[FONT_BIG], font_big_bin);
	fontLoad(&s_fonts[FONT_HUGE], font_huge_bin);
	fontLoad(&s_fonts[FONT_TEXT], font_text_bin);
	fontLoad(&s_fonts[FONT_TEXTB], font_textb_bin);
	fontLoad(&s_fonts[FONT_H1], font_h1_bin);
	fontLoad(&s_fonts[FONT_H2], font_h2_bin);
	iconsLoad();
}

void gfxInvalidate(int which) { s_redraw |= which; }

int gfxTakeRedraw(void) {
	int d = s_redraw;
	s_redraw = 0;
	return d;
}

void gfxMarkDrawn(int which) { s_present |= which; }

// faixas de linhas a copiar no proximo present (0 faixas = tela inteira)
typedef struct Rows {
	int n;
	s16 y0[4], y1[4];
} Rows;
static Rows s_rows[2];

void gfxPresentRows(int which, int y0, int y1) {
	Rows* r = &s_rows[which == GFX_TOP ? 0 : 1];
	if (r->n < 4) {
		r->y0[r->n] = CLAMP(y0, 0, SCR_H);
		r->y1[r->n] = CLAMP(y1, 0, SCR_H);
		r->n++;
	}
}

static void copyScreen(u16* fb, u16* vram, Rows* r) {
	if (!r->n) {
		DC_FlushRange(fb, SCR_W * SCR_H * 2);
		dmaCopyWords(3, fb, vram, SCR_W * SCR_H * 2);
	} else {
		for (int k = 0; k < r->n; k++) {
			if (r->y1[k] <= r->y0[k]) continue;
			u16* src = fb + r->y0[k] * SCR_W;
			int bytes = (r->y1[k] - r->y0[k]) * SCR_W * 2;
			DC_FlushRange(src, bytes);
			dmaCopyWords(3, src, vram + r->y0[k] * SCR_W, bytes);
		}
	}
	r->n = 0;
}

void gfxPresent(void) {
	// chamado logo apos o VBlank; a copia por DMA fica a frente do feixe
	if (s_present & GFX_BOT) copyScreen(s_fbBot, (u16*)BG_GFX, &s_rows[1]);
	if (s_present & GFX_TOP) copyScreen(s_fbTop, (u16*)BG_GFX_SUB, &s_rows[0]);
	s_present = 0;
}

// volta ao modo 2D padrao do DSi Dash (depois do jogo 3D)
void gfxRestore2D(void) {
	videoSetMode(MODE_5_2D);
	videoSetModeSub(MODE_5_2D);
	vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
	vramSetBankB(VRAM_B_LCD);
	vramSetBankC(VRAM_C_SUB_BG_0x06200000);
	vramSetBankD(VRAM_D_LCD);
	vramSetBankE(VRAM_E_LCD);
	REG_BG0CNT = 0;
	REG_BLDCNT = 0;
	bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	bgUpdate();
	lcdMainOnBottom();
	s_rows[0].n = s_rows[1].n = 0;
	gfxInvalidate(GFX_BOTH);
}

void gfxBrightness(int level) { setBrightness(3, level); }

u16 lerpColor(u16 a, u16 b, int t) {
	if (t <= 0) return a;
	if (t >= 256) return b;
	int ar = a & 31, ag = (a >> 5) & 31, ab = (a >> 10) & 31;
	int br = b & 31, bg = (b >> 5) & 31, bb = (b >> 10) & 31;
	int r = ar + (((br - ar) * t) >> 8);
	int g = ag + (((bg - ag) * t) >> 8);
	int bl = ab + (((bb - ab) * t) >> 8);
	return 0x8000 | (bl << 10) | (g << 5) | r;
}

// ---------------------------------------------------------------------------
// primitivas
// ---------------------------------------------------------------------------
void cvSetClip(Canvas* c, int x, int y, int w, int h) {
	c->cx0 = MAX(0, x);
	c->cy0 = MAX(0, y);
	c->cx1 = MIN(SCR_W, x + w);
	c->cy1 = MIN(SCR_H, y + h);
}

void cvResetClip(Canvas* c) {
	c->cx0 = 0;
	c->cy0 = 0;
	c->cx1 = SCR_W;
	c->cy1 = SCR_H;
}

static inline void spanA(Canvas* c, int y, int x0, int x1, u16 col, int a) {
	if (y < c->cy0 || y >= c->cy1 || a <= 0) return;
	if (x0 < c->cx0) x0 = c->cx0;
	if (x1 > c->cx1) x1 = c->cx1;
	if (x0 >= x1) return;
	u16* p = c->px + y * SCR_W + x0;
	int n = x1 - x0;
	if (a >= 32) {
		// 2 pixels por escrita
		if ((u32)p & 2) {
			*p++ = col;
			n--;
		}
		u32 v = col | ((u32)col << 16);
		u32* q = (u32*)p;
		for (int m = n >> 1; m > 0; m--) *q++ = v;
		if (n & 1) *(u16*)q = col;
	} else {
		while (n--) {
			*p = blend(*p, col, a);
			p++;
		}
	}
}

static inline void plotA(Canvas* c, int x, int y, u16 col, int a) {
	if (a <= 0 || x < c->cx0 || x >= c->cx1 || y < c->cy0 || y >= c->cy1) return;
	u16* p = c->px + y * SCR_W + x;
	*p = a >= 32 ? col : blend(*p, col, a);
}

void cvClear(Canvas* c, u16 col) {
	u32 v = col | ((u32)col << 16);
	u32* p = (u32*)c->px;
	for (int i = 0; i < SCR_W * SCR_H / 2; i++) p[i] = v;
}

void cvFill(Canvas* c, int x, int y, int w, int h, u16 col) {
	for (int j = 0; j < h; j++) spanA(c, y + j, x, x + w, col, 32);
}

void cvFillA(Canvas* c, int x, int y, int w, int h, u16 col, int a) {
	for (int j = 0; j < h; j++) spanA(c, y + j, x, x + w, col, a);
}

void cvGrad(Canvas* c, int x, int y, int w, int h, u16 top, u16 bottom) {
	for (int j = 0; j < h; j++) spanA(c, y + j, x, x + w, lerpColor(top, bottom, h > 1 ? j * 256 / (h - 1) : 0), 32);
}

void cvHLine(Canvas* c, int x, int y, int w, u16 col) { spanA(c, y, x, x + w, col, 32); }

void cvVLine(Canvas* c, int x, int y, int h, u16 col) {
	for (int j = 0; j < h; j++) plotA(c, x, y + j, col, 32);
}

// mascara de cobertura de um circulo de raio r (2r x 2r, valores 0..32), 4x4 supersampling
typedef struct CMask {
	int r;
	u8* m;
} CMask;
static CMask s_cm[32];
static int s_cmNext;

static const u8* circMask(int r) {
	for (int i = 0; i < 32; i++)
		if (s_cm[i].m && s_cm[i].r == r) return s_cm[i].m;
	int d = 2 * r;
	u8* m = (u8*)malloc(d * d);
	if (!m) return NULL;
	int R2 = 64 * r * r;
	for (int y = 0; y < d; y++) {
		for (int x = 0; x < d; x++) {
			int cnt = 0;
			for (int ky = 0; ky < 4; ky++) {
				int sy = 8 * y + 2 * ky + 1 - 8 * r;
				for (int kx = 0; kx < 4; kx++) {
					int sx = 8 * x + 2 * kx + 1 - 8 * r;
					if (sx * sx + sy * sy <= R2) cnt++;
				}
			}
			m[y * d + x] = cnt * 2;
		}
	}
	CMask* slot = &s_cm[s_cmNext];
	s_cmNext = (s_cmNext + 1) & 31;
	if (slot->m) free(slot->m);
	slot->r = r;
	slot->m = m;
	return m;
}

// cobertura (0..32) de um retangulo arredondado (w x h, raio r) no pixel (x,y) local
static inline int rrCov(int x, int y, int w, int h, int r, const u8* M) {
	if (x < 0 || y < 0 || x >= w || y >= h) return 0;
	if (r <= 0 || !M) return 32;
	int d = 2 * r, mx, my;
	if (x < r) mx = x;
	else if (x >= w - r) mx = x - (w - d);
	else return 32;
	if (y < r) my = y;
	else if (y >= h - r) my = y - (h - d);
	else return 32;
	return M[my * d + mx];
}

static void rrectFill(Canvas* c, int x, int y, int w, int h, int r, u16 top, u16 bot, int a, bool grad) {
	if (w <= 0 || h <= 0 || a <= 0) return;
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	const u8* M = r > 0 ? circMask(r) : NULL;
	if (!M) r = 0;
	int d = 2 * r;
	for (int j = 0; j < h; j++) {
		int yy = y + j;
		if (yy < c->cy0 || yy >= c->cy1) continue;
		u16 col = grad ? lerpColor(top, bot, h > 1 ? j * 256 / (h - 1) : 0) : top;
		int my = (j < r) ? j : (j >= h - r ? j - (h - d) : -1);
		if (my < 0) {
			spanA(c, yy, x, x + w, col, a);
			continue;
		}
		const u8* mrow = M + my * d;
		for (int i = 0; i < r; i++) {
			int cv = mrow[i];
			if (cv) plotA(c, x + i, yy, col, (cv * a) >> 5);
			cv = mrow[r + i];
			if (cv) plotA(c, x + w - r + i, yy, col, (cv * a) >> 5);
		}
		// meio da linha: borda reta, cobertura = a da coluna central da mascara
		spanA(c, yy, x + r, x + w - r, col, (mrow[r - 1] * a) >> 5);
	}
}

void cvRRect(Canvas* c, int x, int y, int w, int h, int r, u16 col) { rrectFill(c, x, y, w, h, r, col, col, 32, false); }
void cvRRectA(Canvas* c, int x, int y, int w, int h, int r, u16 col, int a) { rrectFill(c, x, y, w, h, r, col, col, a, false); }
void cvRRectGrad(Canvas* c, int x, int y, int w, int h, int r, u16 top, u16 bottom) { rrectFill(c, x, y, w, h, r, top, bottom, 32, true); }

void cvRRectBorder(Canvas* c, int x, int y, int w, int h, int r, int t, u16 col) {
	if (w <= 0 || h <= 0 || t <= 0) return;
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	int ri = MAX(0, r - t);
	int wi = w - 2 * t, hi = h - 2 * t;
	const u8* Mo = r > 0 ? circMask(r) : NULL;
	const u8* Mi = ri > 0 ? circMask(ri) : NULL;
	int band = MAX(r, t + ri);
	for (int j = 0; j < h; j++) {
		int yy = y + j;
		if (yy < c->cy0 || yy >= c->cy1) continue;
		bool mid = (j >= band && j < h - band);
		for (int i = 0; i < w; i++) {
			if (mid && i >= t && i < w - t) {
				i = w - t - 1;
				continue;
			}
			int co = rrCov(i, j, w, h, r, Mo);
			int ci = (wi > 0 && hi > 0) ? rrCov(i - t, j - t, wi, hi, ri, Mi) : 0;
			int cv = co - ci;
			if (cv > 0) plotA(c, x + i, yy, col, cv);
		}
	}
}

void cvShadow(Canvas* c, int x, int y, int w, int h, int r, int a) {
	static const int k4[4] = {1, 2, 3, 4};
	for (int k = 3; k >= 0; k--) {
		int g = 4 - k4[k];  // 3..0
		rrectFill(c, x - g, y - g + 2, w + 2 * g, h + 2 * g, r + g, 0x8000, 0x8000, (a * k4[k]) / 10, false);
	}
}

void cvCircleA(Canvas* c, int cx, int cy, int r, u16 col, int a) {
	if (r <= 0) return;
	const u8* M = circMask(r);
	if (!M) return;
	int d = 2 * r;
	for (int j = 0; j < d; j++) {
		int yy = cy - r + j;
		if (yy < c->cy0 || yy >= c->cy1) continue;
		const u8* row = M + j * d;
		int i0 = 0;
		while (i0 < r && row[i0] == 0) i0++;
		int i1 = d - i0;
		// bordas antialias, interior solido
		int s0 = i0, s1 = i1;
		while (s0 < s1 && row[s0] < 32) {
			plotA(c, cx - r + s0, yy, col, (row[s0] * a) >> 5);
			s0++;
		}
		while (s1 > s0 && row[s1 - 1] < 32) {
			plotA(c, cx - r + s1 - 1, yy, col, (row[s1 - 1] * a) >> 5);
			s1--;
		}
		spanA(c, yy, cx - r + s0, cx - r + s1, col, a);
	}
}

void cvCircle(Canvas* c, int cx, int cy, int r, u16 col) { cvCircleA(c, cx, cy, r, col, 32); }

void cvRing(Canvas* c, int cx, int cy, int r, int t, u16 col) {
	cvRRectBorder(c, cx - r, cy - r, 2 * r, 2 * r, r, t, col);
}

// linha antialias (Xiaolin Wu) em coordenadas inteiras
void cvLine(Canvas* c, int x0, int y0, int x1, int y1, u16 col) {
	int dx = x1 - x0, dy = y1 - y0;
	int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
	if (adx == 0 && ady == 0) {
		plotA(c, x0, y0, col, 32);
		return;
	}
	if (adx >= ady) {
		if (x0 > x1) {
			int t = x0; x0 = x1; x1 = t;
			t = y0; y0 = y1; y1 = t;
			dy = -dy;
		}
		int grad = (dy << 16) / adx;  // 16.16
		int yf = y0 << 16;
		for (int x = x0; x <= x1; x++) {
			int yi = yf >> 16;
			int fr = (yf >> 11) & 31;
			plotA(c, x, yi, col, 32 - fr);
			plotA(c, x, yi + 1, col, fr);
			yf += grad;
		}
	} else {
		if (y0 > y1) {
			int t = x0; x0 = x1; x1 = t;
			t = y0; y0 = y1; y1 = t;
			dx = -dx;
		}
		int grad = (dx << 16) / ady;
		int xf = x0 << 16;
		for (int y = y0; y <= y1; y++) {
			int xi = xf >> 16;
			int fr = (xf >> 11) & 31;
			plotA(c, xi, y, col, 32 - fr);
			plotA(c, xi + 1, y, col, fr);
			xf += grad;
		}
	}
}

int icW(int id) { return (id >= 0 && id < IC_COUNT) ? s_icons[id].w : 0; }
int icH(int id) { return (id >= 0 && id < IC_COUNT) ? s_icons[id].h : 0; }

void cvIconA(Canvas* c, int id, int x, int y, u16 col, int a) {
	if (id < 0 || id >= IC_COUNT) return;
	const IconRef* ic = &s_icons[id];
	for (int j = 0; j < ic->h; j++) {
		int yy = y + j;
		if (yy < c->cy0 || yy >= c->cy1) continue;
		const u8* src = ic->a + j * ic->w;
		for (int i = 0; i < ic->w; i++) {
			int v = src[i];
			if (!v) continue;
			plotA(c, x + i, yy, col, (((v * 33) >> 8) * a) >> 5);
		}
	}
}

void cvIcon(Canvas* c, int id, int x, int y, u16 col) { cvIconA(c, id, x, y, col, 32); }

void cvImage(Canvas* c, const u16* img, int iw, int ih, int x, int y) {
	for (int j = 0; j < ih; j++) {
		int yy = y + j;
		if (yy < c->cy0 || yy >= c->cy1) continue;
		int x0 = MAX(x, c->cx0), x1 = MIN(x + iw, c->cx1);
		if (x0 >= x1) continue;
		memcpy(c->px + yy * SCR_W + x0, img + j * iw + (x0 - x), (x1 - x0) * 2);
	}
}

void cvImageScaled(Canvas* c, const u16* img, int iw, int ih, int x, int y, int w, int h) {
	if (w <= 0 || h <= 0) return;
	int sx = (iw << 16) / w, sy = (ih << 16) / h;
	for (int j = 0; j < h; j++) {
		int yy = y + j;
		if (yy < c->cy0 || yy >= c->cy1) continue;
		const u16* srow = img + ((j * sy) >> 16) * iw;
		u16* drow = c->px + yy * SCR_W;
		int fx = 0;
		for (int i = 0; i < w; i++, fx += sx) {
			int xx = x + i;
			if (xx < c->cx0 || xx >= c->cx1) continue;
			drow[xx] = srow[fx >> 16] | 0x8000;
		}
	}
}

// ---------------------------------------------------------------------------
// texto
// ---------------------------------------------------------------------------
int fontHeight(int f) { return s_fonts[f].lineH; }
int fontAscent(int f) { return s_fonts[f].ascent; }

u32 utf8Next(const char** ps) {
	const u8* s = (const u8*)*ps;
	u32 c = s[0];
	if (c < 0x80) {
		*ps += 1;
		return c;
	}
	if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
		*ps += 2;
		return ((c & 0x1F) << 6) | (s[1] & 0x3F);
	}
	if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
		*ps += 3;
		return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
	}
	if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
		*ps += 4;
		return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
	}
	*ps += 1;  // byte invalido: trata como Latin-1
	return c;
}

static const GlyphRec* glyphFor(const Font* f, u32 cp) {
	int slot = -1;
	if (cp >= 0x20 && cp <= 0xFF) {
		slot = cp - 0x20;
	} else if (cp == 0xA0) {
		slot = 0;
	} else {
		for (int i = 0; i < FONT_EXTRA_COUNT; i++)
			if (g_fontExtraCps[i] == cp) {
				slot = 224 + i;
				break;
			}
		if (slot < 0) {
			// aproximacoes comuns
			if (cp == 0x2212) slot = '-' - 0x20;
			else if (cp == 0x00A0 || cp == 0x2009 || cp == 0x200A || cp == 0x2002 || cp == 0x2003) slot = 0;
			else if (cp == 0x2032) slot = '\'' - 0x20;
			else if (cp == 0x2033) slot = '"' - 0x20;
			else if (cp >= 0x200B && cp <= 0x200F) return NULL;  // largura zero
			else if (cp == 0xFEFF) return NULL;
		}
	}
	if (slot < 0 || slot >= f->count || !f->g[slot].present) {
		slot = '?' - 0x20;
		if (!f->g[slot].present) return NULL;
	}
	return &f->g[slot];
}

int textWidthN(int fi, const char* s, int nbytes) {
	const Font* f = &s_fonts[fi];
	const char* end = nbytes >= 0 ? s + nbytes : NULL;
	int w = 0;
	while ((!end || s < end) && *s) {
		u32 cp = utf8Next(&s);
		if (cp == '\n') break;
		const GlyphRec* g = glyphFor(f, cp);
		if (g) w += g->adv;
	}
	return w;
}

int textWidth(int f, const char* s) { return textWidthN(f, s, -1); }

int textFitBytes(int fi, const char* s, int maxw) {
	const Font* f = &s_fonts[fi];
	const char* p = s;
	int w = 0;
	while (*p) {
		const char* q = p;
		u32 cp = utf8Next(&q);
		const GlyphRec* g = glyphFor(f, cp);
		int a = g ? g->adv : 0;
		if (w + a > maxw) break;
		w += a;
		p = q;
	}
	return (int)(p - s);
}

static void drawGlyph(Canvas* c, const Font* f, const GlyphRec* g, int x, int y, u16 col) {
	const u8* bits = f->bits + g->off;
	int rowb = (g->w + 1) >> 1;
	int gx = x + g->xo, gy = y + g->yo;
	if (gx >= c->cx1 || gy >= c->cy1 || gx + g->w <= c->cx0 || gy + g->h <= c->cy0) return;
	for (int j = 0; j < g->h; j++) {
		int yy = gy + j;
		if (yy < c->cy0 || yy >= c->cy1) continue;
		const u8* r = bits + j * rowb;
		u16* dst = c->px + yy * SCR_W;
		for (int i = 0; i < g->w; i++) {
			int v = (r[i >> 1] >> ((i & 1) << 2)) & 15;
			if (!v) continue;
			int xx = gx + i;
			if (xx < c->cx0 || xx >= c->cx1) continue;
			int a = s_a4[v];
			dst[xx] = a >= 32 ? col : blend(dst[xx], col, a);
		}
	}
}

int cvTextN(Canvas* c, int fi, int x, int y, u16 col, const char* s, int nbytes) {
	const Font* f = &s_fonts[fi];
	const char* end = nbytes >= 0 ? s + nbytes : NULL;
	while ((!end || s < end) && *s) {
		u32 cp = utf8Next(&s);
		if (cp == '\n') break;
		const GlyphRec* g = glyphFor(f, cp);
		if (!g) continue;
		if (g->w) drawGlyph(c, f, g, x, y, col);
		x += g->adv;
		if (x >= c->cx1) {
			// continua medindo nao e necessario; retorna posicao
			while ((!end || s < end) && *s && *s != '\n') {
				u32 cp2 = utf8Next(&s);
				const GlyphRec* g2 = glyphFor(f, cp2);
				if (g2) x += g2->adv;
			}
			break;
		}
	}
	return x;
}

int cvText(Canvas* c, int f, int x, int y, u16 col, const char* s) { return cvTextN(c, f, x, y, col, s, -1); }

void cvTextC(Canvas* c, int f, int cx, int y, u16 col, const char* s) { cvText(c, f, cx - textWidth(f, s) / 2, y, col, s); }

void cvTextR(Canvas* c, int f, int rx, int y, u16 col, const char* s) { cvText(c, f, rx - textWidth(f, s), y, col, s); }

void cvTextFit(Canvas* c, int f, int x, int y, int maxw, u16 col, const char* s) {
	if (textWidth(f, s) <= maxw) {
		cvText(c, f, x, y, col, s);
		return;
	}
	int ell = textWidth(f, "\xE2\x80\xA6");
	int n = textFitBytes(f, s, maxw - ell);
	int ex = cvTextN(c, f, x, y, col, s, n);
	cvText(c, f, ex, y, col, "\xE2\x80\xA6");
}

// quebra de linha por palavras; retorna altura usada
int cvTextWrap(Canvas* c, int f, int x, int y, int w, int maxLines, u16 col, const char* s) {
	int lh = fontHeight(f);
	int lines = 0;
	while (*s && (maxLines <= 0 || lines < maxLines)) {
		while (*s == ' ') s++;
		if (!*s) break;
		int fit = textFitBytes(f, s, w);
		const char* nl = strchr(s, '\n');
		if (nl && nl - s <= fit) fit = (int)(nl - s);
		else if (s[fit]) {
			int k = fit;
			while (k > 0 && s[k] != ' ') k--;
			if (k > 0) fit = k;
		}
		if (fit <= 0) fit = 1;
		bool last = (maxLines > 0 && lines == maxLines - 1 && s[fit] && s[fit] != '\n');
		if (last) {
			char tmp[256];
			int n = MIN((int)strlen(s), 255);
			memcpy(tmp, s, n);
			tmp[n] = 0;
			cvTextFit(c, f, x, y + lines * lh, w, col, tmp);
		} else {
			cvTextN(c, f, x, y + lines * lh, col, s, fit);
		}
		s += fit;
		if (*s == '\n') s++;
		lines++;
	}
	return lines * lh;
}

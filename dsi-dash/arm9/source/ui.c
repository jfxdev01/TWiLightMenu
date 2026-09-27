// DSi Dash — tema, entrada e componentes
#include "common.h"

Theme T;
Input g_in;
u32 g_frame;

void themeApply(bool dark) {
	T.dark = dark;
	if (dark) {
		T.bg = HEX(0x2D2D2D);
		T.surface = HEX(0x3D3D3D);
		T.surface2 = HEX(0x4A4A4A);
		T.text = HEX(0xFFFFFF);
		T.text2 = HEX(0xB4B4B4);
		T.line = HEX(0x5A5A5A);
		T.accent = HEX(0x00C3E3);
		T.accent2 = HEX(0x6CF6FF);
	} else {
		T.bg = HEX(0xEBEBEB);
		T.surface = HEX(0xFFFFFF);
		T.surface2 = HEX(0xF4F4F4);
		T.text = HEX(0x2D2D2D);
		T.text2 = HEX(0x6E6E6E);
		T.line = HEX(0xCDCDCD);
		T.accent = HEX(0x00B4DC);
		T.accent2 = HEX(0x2EE6FF);
	}
	T.onAccent = HEX(0xFFFFFF);
	T.danger = HEX(0xE60012);
	T.ok = HEX(0x1DB954);
	gfxInvalidate(GFX_BOTH);
}

// ---------------------------------------------------------------------------
// entrada
// ---------------------------------------------------------------------------
typedef struct Area {
	int x, y, w, h;
	u32 key;
} Area;
static Area s_areas[12];
static int s_nAreas;
static int s_rep;

void uiResetAreas(void) { s_nAreas = 0; }

static void addArea(int x, int y, int w, int h, u32 key) {
	if (s_nAreas < ARRAY_SIZE(s_areas)) s_areas[s_nAreas++] = (Area){x, y, w, h, key};
}

bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && y >= ry && x < rx + rw && y < ry + rh; }

bool tapIn(int rx, int ry, int rw, int rh) { return g_in.tap && inRect(g_in.sx, g_in.sy, rx, ry, rw, rh); }

void inputUpdate(void) {
	scanKeys();
	u32 down = keysDown(), held = keysHeld(), up = keysUp();
	const u32 repMask = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R;
	u32 rep = down;
	if (held & repMask) {
		if (down & repMask) s_rep = 0;
		s_rep++;
		if (s_rep > 18 && ((s_rep - 18) % 4) == 0) rep |= held & repMask;
	} else {
		s_rep = 0;
	}

	TouchData td;
	bool t = touchRead(&td);
	bool prev = g_in.touch;
	g_in.tDown = t && !prev;
	g_in.tUp = !t && prev;
	if (t) {
		if (g_in.tDown) {
			g_in.sx = g_in.tx = td.px;
			g_in.sy = g_in.ty = td.py;
			g_in.drag = false;
			g_in.frames = 0;
			g_in.dx = g_in.dy = 0;
		} else {
			g_in.dx = td.px - g_in.tx;
			g_in.dy = td.py - g_in.ty;
			g_in.tx = td.px;
			g_in.ty = td.py;
			g_in.frames++;
		}
		if (abs(g_in.tx - g_in.sx) > 6 || abs(g_in.ty - g_in.sy) > 6) g_in.drag = true;
	} else {
		g_in.dx = g_in.dy = 0;
	}
	g_in.touch = t;
	g_in.tap = g_in.tUp && !g_in.drag;

	// toques nas dicas de botao viram teclas
	if (g_in.tap) {
		for (int i = 0; i < s_nAreas; i++) {
			if (inRect(g_in.sx, g_in.sy, s_areas[i].x, s_areas[i].y, s_areas[i].w, s_areas[i].h)) {
				down |= s_areas[i].key;
				rep |= s_areas[i].key;
				g_in.tap = false;  // consumido
				break;
			}
		}
	}
	g_in.down = down;
	g_in.held = held;
	g_in.up = up;
	g_in.rep = rep;
	g_frame++;
}

// ---------------------------------------------------------------------------
// selecao / barras
// ---------------------------------------------------------------------------
u16 uiSelColor(void) {
	int p = g_frame % 64;
	int t = p < 32 ? p : 64 - p;  // 0..32
	return lerpColor(T.accent, T.accent2, t * 8);
}

void uiSelBorder(Canvas* c, int x, int y, int w, int h, int r) {
	cvRRectBorder(c, x - 4, y - 4, w + 8, h + 8, r + 4, 3, uiSelColor());
}

static const u32 s_favColors[16] = {
	0x61829A, 0xBA4900, 0xFF0018, 0xFF8AC3, 0xFB9200, 0xF3E300, 0xAAFB00, 0x00FB00,
	0x00A238, 0x49DB8A, 0x30BAF3, 0x0059F3, 0x000092, 0x8A00D3, 0xD300EB, 0xFF0092,
};

u16 uiFavColor(void) { return HEX(s_favColors[sysFavColorIndex() & 15]); }

void uiStatusBar(Canvas* c, bool overlay) {
	u16 fg = overlay ? HEX(0xFFFFFF) : T.text;
	// avatar
	cvCircle(c, 12, 11, 8, uiFavColor());
	char ini[8] = {0};
	const char* nick = sysNick();
	const char* p = nick;
	utf8Next(&p);
	memcpy(ini, nick, MIN((int)(p - nick), 4));
	cvTextC(c, FONT_SMALL, 12, 3, HEX(0xFFFFFF), ini);
	cvTextFit(c, FONT_SMALL, 24, 3, 110, fg, nick);

	// bateria
	unsigned bs = sysBattery();
	int lvl = bs & 0x7F;
	bool chg = bs & 0x80;
	int bx = 228, by = 6;
	cvRRectBorder(c, bx, by, 20, 10, 3, 1, fg);
	cvFill(c, bx + 20, by + 3, 2, 4, fg);
	int fw = (16 * CLAMP(lvl, 0, 15) + 14) / 15;
	u16 fc = chg ? HEX(0x2ECC71) : (lvl <= 3 ? T.danger : fg);
	if (fw > 0) cvRRect(c, bx + 2, by + 2, fw, 6, 1, fc);

	// wi-fi (4 barras)
	int bars = netBars();  // -1 = sem conexao
	int wx = 206;
	for (int i = 0; i < 4; i++) {
		int h = 3 + i * 2;
		bool on = bars >= 0 && i <= bars;
		cvFillA(c, wx + i * 4, 16 - h, 3, h, fg, on ? 32 : 9);
	}
	if (bars < 0 && netState() == NET_CONNECTING && (g_frame / 20) % 2)
		cvFillA(c, wx, 13, 15, 3, fg, 20);

	// hora
	char tb[12];
	sysFormatTime(tb, sizeof(tb));
	cvTextR(c, FONT_BODY, 200, 1, fg, tb);
}

int uiButtonGlyphW(u32 key) {
	switch (key) {
		case KEY_START: return textWidth(FONT_SMALL, "START") + 8;
		case KEY_SELECT: return textWidth(FONT_SMALL, "SELECT") + 8;
		case KEY_L: case KEY_R: return 20;
		default: return 15;
	}
}

void uiButtonGlyph(Canvas* c, int x, int y, u32 key) {
	const char* s = "?";
	switch (key) {
		case KEY_A: s = "A"; break;
		case KEY_B: s = "B"; break;
		case KEY_X: s = "X"; break;
		case KEY_Y: s = "Y"; break;
		case KEY_L: s = "L"; break;
		case KEY_R: s = "R"; break;
		case KEY_START: s = "START"; break;
		case KEY_SELECT: s = "SELECT"; break;
		case KEY_UP | KEY_DOWN: s = "\xE2\x86\x95"; break;
	}
	int w = uiButtonGlyphW(key);
	if (w == 15) {
		cvCircle(c, x + 7, y + 7, 7, T.text);
		cvTextC(c, FONT_SMALL, x + 7, y - 1, T.bg, s);
	} else {
		cvRRect(c, x, y, w, 14, 7, T.text);
		cvTextC(c, FONT_SMALL, x + w / 2, y - 1, T.bg, s);
	}
}

static int hintsWidth(const Hint* h, int n, int font, int gap) {
	int w = 0;
	for (int i = 0; i < n; i++) w += uiButtonGlyphW(h[i].key) + 4 + textWidth(font, h[i].label) + (i ? gap : 0);
	return w;
}

void uiHints(Canvas* c, const Hint* h, int n) {
	cvFill(c, 0, HINTS_Y, SCR_W, SCR_H - HINTS_Y, T.bg);
	cvHLine(c, 8, HINTS_Y, SCR_W - 16, T.line);
	// cabe? senao usa fonte menor e, em ultimo caso, descarta as primeiras dicas
	int font = FONT_BODY, gap = 14;
	const int avail = SCR_W - 16;
	if (hintsWidth(h, n, font, gap) > avail) {
		font = FONT_SMALL;
		gap = 9;
	}
	while (n > 1 && hintsWidth(h, n, font, gap) > avail) {
		h++;
		n--;
	}
	int ty = HINTS_Y + (font == FONT_BODY ? 1 : 3);
	int x = SCR_W - 10;
	for (int i = n - 1; i >= 0; i--) {
		int gw = uiButtonGlyphW(h[i].key);
		int lw = textWidth(font, h[i].label);
		int w = gw + 4 + lw;
		x -= w;
		uiButtonGlyph(c, x, HINTS_Y + 4, h[i].key);
		cvText(c, font, x + gw + 4, ty, T.text, h[i].label);
		addArea(x - 5, HINTS_Y, w + 10, SCR_H - HINTS_Y, h[i].key);
		x -= gap;
	}
}

void uiBackButton(Canvas* c) {
	cvCircle(c, 16, 14, 11, T.surface);
	cvIcon(c, IC_BACK_14, 9, 7, T.text);
	addArea(0, 0, 34, 30, KEY_B);
}

// ---------------------------------------------------------------------------
// componentes
// ---------------------------------------------------------------------------
static const s8 s_dots[8][2] = {{0, -10}, {7, -7}, {10, 0}, {7, 7}, {0, 10}, {-7, 7}, {-10, 0}, {-7, -7}};

void uiSpinner(Canvas* c, int cx, int cy, int r, u16 col) {
	int head = (g_frame / 4) & 7;
	for (int i = 0; i < 8; i++) {
		int age = (head - i + 8) & 7;
		int a = 32 - age * 4;
		cvCircleA(c, cx + s_dots[i][0] * r / 10, cy + s_dots[i][1] * r / 10, MAX(2, r / 5), col, a);
	}
}

static char s_toast[96];
static int s_toastT;

void uiToast(const char* msg) {
	snprintf(s_toast, sizeof(s_toast), "%s", msg);
	s_toastT = 150;
	gfxInvalidate(GFX_BOT);
}

bool uiToastActive(void) { return s_toastT > 0; }

void uiDrawToast(Canvas* c) {
	if (s_toastT <= 0) return;
	s_toastT--;
	int a = s_toastT < 16 ? s_toastT * 2 : 32;
	int w = MIN(textWidth(FONT_BODY, s_toast) + 24, 240);
	int x = (SCR_W - w) / 2, y = 136;
	cvRRectA(c, x, y, w, 26, 13, HEX(0x202020), (a * 7) / 8);
	if (a > 8) cvTextFit(c, FONT_BODY, x + 12, y + 3, w - 24, HEX(0xFFFFFF), s_toast);
	gfxInvalidate(GFX_BOT);
}

void uiButton(Canvas* c, int x, int y, int w, int h, const char* label, bool primary, bool selected) {
	if (!primary) cvShadow(c, x, y, w, h, h / 2, 6);
	cvRRect(c, x, y, w, h, h / 2, primary ? T.accent : T.surface);
	int f = FONT_BODY;
	cvTextC(c, f, x + w / 2, y + (h - fontHeight(f)) / 2, primary ? T.onAccent : T.text, label);
	if (selected) uiSelBorder(c, x, y, w, h, h / 2);
}

void uiHeader(Canvas* c, int icon, const char* title) {
	if (icon >= 0) cvIcon(c, icon, 10, STATUS_H + 4, T.accent);
	cvText(c, FONT_TITLE, icon >= 0 ? 36 : 12, STATUS_H + 2, T.text, title);
	cvHLine(c, 8, STATUS_H + 28, SCR_W - 16, T.line);
}

void uiEmpty(Canvas* c, int icon, const char* msg, const char* sub) {
	int y = 56;
	if (icon >= 0) cvIcon(c, icon, (SCR_W - icW(icon)) / 2, y, T.text2);
	cvTextC(c, FONT_TITLE, SCR_W / 2, y + 48, T.text, msg);
	if (sub) cvTextWrap(c, FONT_SMALL, 20, y + 72, SCR_W - 40, 3, T.text2, sub);
}

void uiProgress(Canvas* c, int x, int y, int w, int pct) {
	cvRRect(c, x, y, w, 6, 3, T.line);
	int fw = w * CLAMP(pct, 0, 100) / 100;
	if (fw > 0) cvRRect(c, x, y, MAX(6, fw), 6, 3, T.accent);
}

// ---------------------------------------------------------------------------
// dialogo
// ---------------------------------------------------------------------------
static struct {
	bool on;
	char title[48];
	char msg[160];
	const char* opts[4];
	int n, sel;
	DialogFn fn;
} s_dlg;

void uiDialog(const char* title, const char* msg, const char* const* opts, int nopts, DialogFn fn) {
	s_dlg.on = true;
	snprintf(s_dlg.title, sizeof(s_dlg.title), "%s", title ? title : "");
	snprintf(s_dlg.msg, sizeof(s_dlg.msg), "%s", msg ? msg : "");
	s_dlg.n = MIN(nopts, 4);
	for (int i = 0; i < s_dlg.n; i++) s_dlg.opts[i] = opts[i];
	s_dlg.sel = 0;
	s_dlg.fn = fn;
	gfxInvalidate(GFX_BOT);
}

bool uiDialogActive(void) { return s_dlg.on; }

static int dlgLayout(int* bx, int* by, int* bw, int* bh, int* oy) {
	*bw = 224;
	*bx = (SCR_W - *bw) / 2;
	int msgH = s_dlg.msg[0] ? 2 * fontHeight(FONT_SMALL) + 4 : 0;
	*bh = 30 + msgH + s_dlg.n * 28 + 6;
	*by = (SCR_H - *bh) / 2;
	*oy = *by + 30 + msgH;
	return 0;
}

static void dlgClose(int choice) {
	s_dlg.on = false;
	gfxInvalidate(GFX_BOTH);
	if (s_dlg.fn) s_dlg.fn(choice);
}

void uiDialogFrame(void) {
	int bx, by, bw, bh, oy;
	dlgLayout(&bx, &by, &bw, &bh, &oy);
	if (g_in.rep & KEY_DOWN) s_dlg.sel = (s_dlg.sel + 1) % s_dlg.n;
	if (g_in.rep & KEY_UP) s_dlg.sel = (s_dlg.sel + s_dlg.n - 1) % s_dlg.n;
	if (g_in.down & KEY_A) {
		dlgClose(s_dlg.sel);
		return;
	}
	if (g_in.down & KEY_B) {
		dlgClose(-1);
		return;
	}
	if (g_in.tap) {
		for (int i = 0; i < s_dlg.n; i++) {
			if (inRect(g_in.sx, g_in.sy, bx + 8, oy + i * 28, bw - 16, 26)) {
				dlgClose(i);
				return;
			}
		}
		if (!inRect(g_in.sx, g_in.sy, bx, by, bw, bh)) {
			dlgClose(-1);
			return;
		}
	}
	gfxInvalidate(GFX_BOT);
}

void uiDialogDraw(Canvas* c) {
	if (!s_dlg.on) return;
	int bx, by, bw, bh, oy;
	dlgLayout(&bx, &by, &bw, &bh, &oy);
	cvFillA(c, 0, 0, SCR_W, SCR_H, HEX(0x000000), 14);
	cvShadow(c, bx, by, bw, bh, 12, 10);
	cvRRect(c, bx, by, bw, bh, 12, T.surface);
	cvTextC(c, FONT_TITLE, SCR_W / 2, by + 6, T.text, s_dlg.title);
	if (s_dlg.msg[0]) cvTextWrap(c, FONT_SMALL, bx + 12, by + 30, bw - 24, 2, T.text2, s_dlg.msg);
	for (int i = 0; i < s_dlg.n; i++) {
		int y = oy + i * 28;
		bool sel = i == s_dlg.sel;
		cvRRect(c, bx + 8, y, bw - 16, 24, 12, sel ? T.surface2 : T.surface);
		cvTextC(c, FONT_BODY, SCR_W / 2, y + 3, T.text, s_dlg.opts[i]);
		if (sel) cvRRectBorder(c, bx + 6, y - 2, bw - 12, 28, 14, 2, uiSelColor());
	}
}

// ---------------------------------------------------------------------------
// lista
// ---------------------------------------------------------------------------
void lvInit(ListView* lv, int x, int y, int w, int h, int rowH, int count, ListDrawFn fn) {
	memset(lv, 0, sizeof(*lv));
	lv->x = x;
	lv->y = y;
	lv->w = w;
	lv->h = h;
	lv->rowH = rowH;
	lv->count = count;
	lv->draw = fn;
}

static int lvMax(ListView* lv) { return MAX(0, lv->count * lv->rowH - lv->h); }

void lvEnsureVisible(ListView* lv) {
	int top = lv->sel * lv->rowH;
	if (top < lv->scroll) lv->scroll = top;
	if (top + lv->rowH > lv->scroll + lv->h) lv->scroll = top + lv->rowH - lv->h;
	lv->scroll = CLAMP(lv->scroll, 0, lvMax(lv));
}

int lvUpdate(ListView* lv) {
	int act = -1;
	if (lv->count <= 0) return -1;
	if (lv->sel >= lv->count) lv->sel = lv->count - 1;
	int page = MAX(1, lv->h / lv->rowH - 1);
	int old = lv->sel;
	if (g_in.rep & KEY_DOWN) lv->sel = MIN(lv->count - 1, lv->sel + 1);
	if (g_in.rep & KEY_UP) lv->sel = MAX(0, lv->sel - 1);
	if (g_in.rep & KEY_R) lv->sel = MIN(lv->count - 1, lv->sel + page);
	if (g_in.rep & KEY_L) lv->sel = MAX(0, lv->sel - page);
	if (old != lv->sel) {
		lvEnsureVisible(lv);
		gfxInvalidate(GFX_BOTH);
	}
	if (g_in.down & KEY_A) act = lv->sel;

	if (g_in.tDown && inRect(g_in.tx, g_in.ty, lv->x, lv->y, lv->w, lv->h)) {
		lv->dragging = true;
		lv->grabScroll = lv->scroll;
		lv->vel = 0;
	}
	if (lv->dragging && g_in.touch && g_in.drag) {
		int ns = CLAMP(lv->grabScroll - (g_in.ty - g_in.sy), 0, lvMax(lv));
		if (ns != lv->scroll) gfxInvalidate(GFX_BOT);
		lv->scroll = ns;
		lv->vel = -g_in.dy * 256;
	}
	if (lv->dragging && !g_in.touch) {
		lv->dragging = false;
		if (g_in.tap) {
			int idx = (g_in.sy - lv->y + lv->scroll) / lv->rowH;
			if (idx >= 0 && idx < lv->count) {
				lv->sel = idx;
				act = idx;
				gfxInvalidate(GFX_BOTH);
			}
		}
	}
	if (!lv->dragging && lv->vel) {
		lv->scroll = CLAMP(lv->scroll + lv->vel / 256, 0, lvMax(lv));
		lv->vel = lv->vel * 7 / 8;
		if (abs(lv->vel) < 128) lv->vel = 0;
		gfxInvalidate(GFX_BOT);
	}
	return act;
}

void lvDraw(Canvas* c, ListView* lv) {
	cvSetClip(c, lv->x, lv->y, lv->w, lv->h);
	int first = lv->scroll / lv->rowH;
	for (int i = MAX(0, first); i < lv->count; i++) {
		int y = lv->y + i * lv->rowH - lv->scroll;
		if (y >= lv->y + lv->h) break;
		lv->draw(c, lv, i, lv->x, y, lv->w, lv->rowH, i == lv->sel);
	}
	cvResetClip(c);
	int total = lv->count * lv->rowH;
	if (total > lv->h) {
		int th = MAX(14, lv->h * lv->h / total);
		int ty = lv->y + (lv->h - th) * lv->scroll / MAX(1, lvMax(lv));
		cvRRect(c, lv->x + lv->w - 3, ty, 3, th, 1, T.line);
	}
}

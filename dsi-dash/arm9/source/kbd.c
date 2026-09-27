// DSi Dash — teclado na tela (estilo Switch), QWERTY com acentos
#include "common.h"

#define KB_MAX 512

enum { PAGE_ABC, PAGE_SYM, PAGE_ACC };
enum { K_CHAR, K_SHIFT, K_BKSP, K_PAGE, K_SPACE, K_OK, K_ACC };

typedef struct Key {
	int x, y, w;
	int type;
	const char* lo;  // UTF-8
	const char* up;
} Key;

static struct {
	bool on;
	char title[64];
	char text[KB_MAX];
	int maxLen;
	int page;
	bool shift, caps;
	int cur;  // indice da tecla com foco (d-pad)
	KeyboardFn fn;
	Key keys[64];
	int nkeys;
	int pressed, pressT;
} K;

#define KY0 60
#define KH 24
#define KG 2

static void addKey(int x, int row, int w, int type, const char* lo, const char* up) {
	if (K.nkeys >= 64) return;
	K.keys[K.nkeys++] = (Key){x, KY0 + row * (KH + KG), w, type, lo, up};
}

static void addRow(int row, int x, const char* const* lo, const char* const* up, int n) {
	for (int i = 0; i < n; i++) addKey(x + i * 24, row, 22, K_CHAR, lo[i], up ? up[i] : lo[i]);
}

static void layout(void) {
	K.nkeys = 0;
	static const char* num[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
	if (K.page == PAGE_ABC) {
		static const char* r1[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"};
		static const char* R1[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"};
		static const char* r2[] = {"a", "s", "d", "f", "g", "h", "j", "k", "l", "\xC3\xA7"};
		static const char* R2[] = {"A", "S", "D", "F", "G", "H", "J", "K", "L", "\xC3\x87"};
		static const char* r3[] = {"z", "x", "c", "v", "b", "n", "m"};
		static const char* R3[] = {"Z", "X", "C", "V", "B", "N", "M"};
		addRow(0, 8, num, NULL, 10);
		addRow(1, 8, r1, R1, 10);
		addRow(2, 8, r2, R2, 10);
		addKey(8, 3, 34, K_SHIFT, "", "");
		for (int i = 0; i < 7; i++) addKey(44 + i * 24, 3, 22, K_CHAR, r3[i], R3[i]);
		addKey(212, 3, 36, K_BKSP, "", "");
	} else if (K.page == PAGE_SYM) {
		static const char* s1[] = {"@", "#", "$", "%", "&", "*", "-", "+", "(", ")"};
		static const char* s2[] = {"!", "\"", "'", ":", ";", "/", "?", "_", "=", "~"};
		static const char* s3[] = {",", ".", "<", ">", "[", "]", "|"};
		addRow(0, 8, num, NULL, 10);
		addRow(1, 8, s1, NULL, 10);
		addRow(2, 8, s2, NULL, 10);
		addKey(8, 3, 34, K_ACC, "", "");
		for (int i = 0; i < 7; i++) addKey(44 + i * 24, 3, 22, K_CHAR, s3[i], s3[i]);
		addKey(212, 3, 36, K_BKSP, "", "");
	} else {
		static const char* a1[] = {"\xC3\xA1", "\xC3\xA0", "\xC3\xA2", "\xC3\xA3", "\xC3\xA9", "\xC3\xAA", "\xC3\xAD", "\xC3\xB3", "\xC3\xB4", "\xC3\xB5"};
		static const char* A1[] = {"\xC3\x81", "\xC3\x80", "\xC3\x82", "\xC3\x83", "\xC3\x89", "\xC3\x8A", "\xC3\x8D", "\xC3\x93", "\xC3\x94", "\xC3\x95"};
		static const char* a2[] = {"\xC3\xBA", "\xC3\xBC", "\xC3\xA7", "\xC3\xB1", "\xC2\xBA", "\xC2\xAA", "\xE2\x82\xAC", "\xC2\xB0", "{", "}"};
		static const char* A2[] = {"\xC3\x9A", "\xC3\x9C", "\xC3\x87", "\xC3\x91", "\xC2\xBA", "\xC2\xAA", "\xE2\x82\xAC", "\xC2\xB0", "{", "}"};
		static const char* a3[] = {"\\", "^", "`", "\xC2\xA7", "\xC2\xA3", "\xC2\xBF", "\xC2\xA1"};
		addRow(0, 8, num, NULL, 10);
		addRow(1, 8, a1, A1, 10);
		addRow(2, 8, a2, A2, 10);
		addKey(8, 3, 34, K_SHIFT, "", "");
		for (int i = 0; i < 7; i++) addKey(44 + i * 24, 3, 22, K_CHAR, a3[i], a3[i]);
		addKey(212, 3, 36, K_BKSP, "", "");
	}
	addKey(8, 4, 34, K_PAGE, "", "");
	addKey(44, 4, 22, K_CHAR, "/", "/");
	addKey(68, 4, 106, K_SPACE, " ", " ");
	addKey(176, 4, 22, K_CHAR, ".", ".");
	addKey(200, 4, 48, K_OK, "", "");
	if (K.cur >= K.nkeys) K.cur = K.nkeys - 1;
}

void kbdOpen(const char* title, const char* initial, int maxLen, KeyboardFn fn) {
	memset(&K, 0, sizeof(K));
	K.on = true;
	snprintf(K.title, sizeof(K.title), "%s", title ? title : "");
	snprintf(K.text, sizeof(K.text), "%s", initial ? initial : "");
	K.maxLen = CLAMP(maxLen, 1, KB_MAX - 1);
	K.fn = fn;
	K.pressed = -1;
	layout();
	K.cur = 12;
	gfxInvalidate(GFX_BOTH);
}

bool kbdActive(void) { return K.on; }

static void finish(bool ok) {
	K.on = false;
	gfxInvalidate(GFX_BOTH);
	if (K.fn) K.fn(ok ? K.text : NULL);
}

static void backspace(void) {
	int n = strlen(K.text);
	if (!n) return;
	n--;
	while (n > 0 && ((u8)K.text[n] & 0xC0) == 0x80) n--;  // volta ate o inicio do char UTF-8
	K.text[n] = 0;
}

static void press(int i) {
	Key* k = &K.keys[i];
	K.pressed = i;
	K.pressT = 6;
	switch (k->type) {
		case K_CHAR:
		case K_SPACE: {
			const char* s = (K.shift || K.caps) ? k->up : k->lo;
			if ((int)(strlen(K.text) + strlen(s)) <= K.maxLen) strcat(K.text, s);
			if (K.shift && !K.caps) K.shift = false;
			break;
		}
		case K_SHIFT:
			if (K.shift && !K.caps) K.caps = true;
			else if (K.caps) K.shift = K.caps = false;
			else K.shift = true;
			break;
		case K_BKSP: backspace(); break;
		case K_PAGE:
			K.page = (K.page == PAGE_ABC) ? PAGE_SYM : PAGE_ABC;
			layout();
			break;
		case K_ACC:
			K.page = PAGE_ACC;
			layout();
			break;
		case K_OK: finish(true); return;
	}
	gfxInvalidate(GFX_BOTH);
}

// navegacao por d-pad: tecla mais proxima na direcao
static void move(int dx, int dy) {
	Key* c = &K.keys[K.cur];
	int cx = c->x + c->w / 2, cy = c->y + KH / 2;
	int best = -1, bestD = 1 << 30;
	for (int i = 0; i < K.nkeys; i++) {
		if (i == K.cur) continue;
		Key* k = &K.keys[i];
		int kx = k->x + k->w / 2, ky = k->y + KH / 2;
		int ddx = kx - cx, ddy = ky - cy;
		if (dx && (ddx * dx <= 0 || abs(ddy) > 4)) continue;
		if (dy && (ddy * dy <= 0)) continue;
		int d = dy ? abs(ddy) * 64 + abs(ddx) : abs(ddx);
		if (d < bestD) {
			bestD = d;
			best = i;
		}
	}
	if (best < 0 && dx) {  // da a volta na linha
		for (int i = 0; i < K.nkeys; i++) {
			Key* k = &K.keys[i];
			if (k->y != c->y) continue;
			if (best < 0 || (dx > 0 ? k->x < K.keys[best].x : k->x > K.keys[best].x)) best = i;
		}
	}
	if (best >= 0) K.cur = best;
	gfxInvalidate(GFX_BOT);
}

void kbdFrame(void) {
	if (!K.on) return;
	if (g_in.rep & KEY_LEFT) move(-1, 0);
	if (g_in.rep & KEY_RIGHT) move(1, 0);
	if (g_in.rep & KEY_UP) move(0, -1);
	if (g_in.rep & KEY_DOWN) move(0, 1);
	if (g_in.down & KEY_A) press(K.cur);
	if (!K.on) return;
	if (g_in.rep & KEY_B) {
		backspace();
		gfxInvalidate(GFX_BOTH);
	}
	if (g_in.down & KEY_Y) press(K.nkeys - 3);  // espaco
	if (g_in.down & KEY_L) {
		K.shift = !K.shift;
		K.caps = false;
		gfxInvalidate(GFX_BOT);
	}
	if (g_in.down & KEY_R) {
		K.page = (K.page + 1) % 3;
		layout();
		gfxInvalidate(GFX_BOT);
	}
	if (g_in.down & KEY_START) {
		finish(true);
		return;
	}
	if (g_in.down & KEY_SELECT) {
		finish(false);
		return;
	}
	if (g_in.tap) {
		if (inRect(g_in.sx, g_in.sy, 0, 0, 36, 34)) {
			finish(false);
			return;
		}
		for (int i = 0; i < K.nkeys; i++) {
			Key* k = &K.keys[i];
			if (inRect(g_in.sx, g_in.sy, k->x - 1, k->y - 1, k->w + 2, KH + 2)) {
				K.cur = i;
				press(i);
				return;
			}
		}
	}
	if (K.pressT > 0 && --K.pressT == 0) {
		K.pressed = -1;
		gfxInvalidate(GFX_BOT);
	}
}

void kbdDrawTop(Canvas* c) {
	cvClear(c, T.bg);
	uiStatusBar(c, false);
	uiHeader(c, IC_SEARCH_20, K.title[0] ? K.title : "Digite");
	cvRRect(c, 10, 60, SCR_W - 20, 100, 10, T.surface);
	int h = cvTextWrap(c, FONT_BODY, 18, 66, SCR_W - 36, 5, T.text, K.text);
	(void)h;
	cvTextC(c, FONT_SMALL, SCR_W / 2, 166, T.text2, "START confirma  \xE2\x80\xA2  SELECT cancela  \xE2\x80\xA2  B apaga");
}

void kbdDrawBot(Canvas* c) {
	u16 kbBg = T.dark ? HEX(0x232323) : HEX(0xDCDCDC);
	u16 keyC = T.dark ? HEX(0x464646) : HEX(0xFFFFFF);
	u16 keyS = T.dark ? HEX(0x353535) : HEX(0xC4C4C4);
	cvClear(c, T.bg);
	// campo de texto
	cvCircle(c, 17, 17, 11, T.surface);
	cvIcon(c, IC_CLOSE_14, 10, 10, T.text);
	cvRRect(c, 34, 6, SCR_W - 42, 24, 12, T.surface);
	cvRRectBorder(c, 34, 6, SCR_W - 42, 24, 12, 2, T.accent);
	const char* s = K.text;
	int maxw = SCR_W - 64;
	while (textWidth(FONT_BODY, s) > maxw) utf8Next(&s);  // mostra o final
	int ex = cvText(c, FONT_BODY, 44, 8, T.text, s);
	if ((g_frame / 30) & 1) cvFill(c, ex + 1, 11, 2, 15, T.accent);
	cvTextFit(c, FONT_SMALL, 10, 36, SCR_W - 20, T.text2, K.title);

	cvFill(c, 0, KY0 - 6, SCR_W, SCR_H - KY0 + 6, kbBg);
	for (int i = 0; i < K.nkeys; i++) {
		Key* k = &K.keys[i];
		bool special = k->type != K_CHAR && k->type != K_SPACE;
		u16 bg = special ? keyS : keyC;
		if (k->type == K_OK) bg = T.accent;
		if (i == K.pressed) bg = T.accent2;
		cvRRect(c, k->x, k->y, k->w, KH, 5, bg);
		u16 fg = (k->type == K_OK) ? T.onAccent : T.text;
		int cx = k->x + k->w / 2;
		switch (k->type) {
			case K_CHAR: cvTextC(c, FONT_BODY, cx, k->y + 2, fg, (K.shift || K.caps) ? k->up : k->lo); break;
			case K_SPACE: cvTextC(c, FONT_SMALL, cx, k->y + 4, T.text2, "espa\xC3\xA7o"); break;
			case K_SHIFT:
				cvIcon(c, IC_SHIFT_20, cx - 10, k->y + 2, (K.shift || K.caps) ? T.accent : fg);
				if (K.caps) cvFill(c, cx - 5, k->y + 20, 10, 2, T.accent);
				break;
			case K_BKSP: cvIcon(c, IC_BKSP_20, cx - 10, k->y + 2, fg); break;
			case K_PAGE: cvTextC(c, FONT_SMALL, cx, k->y + 4, fg, K.page == PAGE_ABC ? "?123" : "ABC"); break;
			case K_ACC: cvTextC(c, FONT_BODY, cx, k->y + 2, fg, "\xC3\xA1\xC3\xA9"); break;
			case K_OK: cvTextC(c, FONT_BODY, cx, k->y + 2, fg, "OK"); break;
		}
		if (i == K.cur) cvRRectBorder(c, k->x - 2, k->y - 2, k->w + 4, KH + 4, 7, 2, uiSelColor());
	}
	gfxInvalidate(GFX_BOT);  // cursor piscando / borda animada
}

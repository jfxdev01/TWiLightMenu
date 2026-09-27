// DSi Dash — Calculadora (expressoes com + - x / % e parenteses)
#include "common.h"

static char s_expr[64];
static char s_result[48];
static int s_cur;  // tecla com foco

static const char* KEYS[20] = {
	"C", "(", ")", "\xC3\xB7",
	"7", "8", "9", "\xC3\x97",
	"4", "5", "6", "-",
	"1", "2", "3", "+",
	"0", ",", "\xE2\x8C\xAB", "=",
};

// --- avaliador por descida recursiva ---
static const char* P;
static bool s_err;

static double expr(void);

static void skip(void) {
	while (*P == ' ') P++;
}

static double number(void) {
	skip();
	if (*P == '(') {
		P++;
		double v = expr();
		skip();
		if (*P == ')') P++;
		return v;
	}
	if (*P == '-') {
		P++;
		return -number();
	}
	char* e;
	double v = strtod(P, &e);
	if (e == P) s_err = true;
	P = e;
	skip();
	if (*P == '%') {
		P++;
		v /= 100.0;
	}
	return v;
}

static double term(void) {
	double v = number();
	for (;;) {
		skip();
		if (*P == '*') {
			P++;
			v *= number();
		} else if (*P == '/') {
			P++;
			double d = number();
			if (d == 0) s_err = true;
			else v /= d;
		} else return v;
	}
}

static double expr(void) {
	double v = term();
	for (;;) {
		skip();
		if (*P == '+') {
			P++;
			v += term();
		} else if (*P == '-') {
			P++;
			v -= term();
		} else return v;
	}
}

static void evaluate(void) {
	char e[64];
	int n = 0;
	for (const char* s = s_expr; *s && n < 62;) {
		if (!strncmp(s, "\xC3\x97", 2)) e[n++] = '*', s += 2;
		else if (!strncmp(s, "\xC3\xB7", 2)) e[n++] = '/', s += 2;
		else if (*s == ',') e[n++] = '.', s++;
		else e[n++] = *s++;
	}
	e[n] = 0;
	s_err = false;
	P = e;
	double v = expr();
	if (*P) s_err = true;
	if (s_err) {
		snprintf(s_result, sizeof(s_result), "Erro");
		return;
	}
	snprintf(s_result, sizeof(s_result), "%.10g", v);
	for (char* c = s_result; *c; c++)
		if (*c == '.') *c = ',';
}

static void press(int k) {
	const char* t = KEYS[k];
	int len = strlen(s_expr);
	if (!strcmp(t, "C")) {
		s_expr[0] = 0;
		s_result[0] = 0;
	} else if (!strcmp(t, "\xE2\x8C\xAB")) {
		if (len) {
			len--;
			while (len > 0 && ((u8)s_expr[len] & 0xC0) == 0x80) len--;
			s_expr[len] = 0;
		}
	} else if (!strcmp(t, "=")) {
		evaluate();
		if (strcmp(s_result, "Erro")) snprintf(s_expr, sizeof(s_expr), "%s", s_result);
	} else if (len + (int)strlen(t) < (int)sizeof(s_expr) - 1) {
		strcat(s_expr, t);
	}
	sndMove();
	gfxInvalidate(GFX_BOTH);
}

#define KX 16
#define KY 22
#define KW 52
#define KH 26
#define KGX 4
#define KGY 3

static void cFrame(void) {
	if (g_in.down & KEY_B) {
		sndBack();
		appHome();
		return;
	}
	if (g_in.rep & KEY_RIGHT) s_cur = (s_cur / 4) * 4 + (s_cur % 4 + 1) % 4;
	if (g_in.rep & KEY_LEFT) s_cur = (s_cur / 4) * 4 + (s_cur % 4 + 3) % 4;
	if (g_in.rep & KEY_DOWN) s_cur = (s_cur + 4) % 20;
	if (g_in.rep & KEY_UP) s_cur = (s_cur + 16) % 20;
	if (g_in.down & KEY_A) press(s_cur);
	if (g_in.down & KEY_START) press(19);
	if (g_in.down & KEY_Y) press(18);
	if (g_in.tap) {
		for (int k = 0; k < 20; k++) {
			int x = KX + (k % 4) * (KW + KGX), y = KY + (k / 4) * (KH + KGY);
			if (inRect(g_in.sx, g_in.sy, x, y, KW, KH)) {
				s_cur = k;
				press(k);
			}
		}
	}
	gfxInvalidate(GFX_BOT);
}

static void cDrawTop(Canvas* c) {
	cvClear(c, T.bg);
	uiStatusBar(c, false);
	uiHeader(c, IC_CALC_20, "Calculadora");
	cvRRect(c, 10, 60, SCR_W - 20, 110, 14, T.surface);
	const char* e = s_expr;
	while (textWidth(FONT_TITLE, e) > SCR_W - 44) utf8Next(&e);
	cvTextR(c, FONT_TITLE, SCR_W - 22, 70, T.text2, e[0] ? e : "0");
	const char* r = s_result[0] ? s_result : "";
	int f = textWidth(FONT_HUGE, r) < SCR_W - 40 && !strpbrk(r, "Ee") ? FONT_HUGE : FONT_BIG;
	if (f == FONT_BIG && strcmp(r, "Erro") == 0) f = FONT_BIG;
	cvTextR(c, f, SCR_W - 20, f == FONT_HUGE ? 98 : 118, T.text, r);
}

static void cDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	uiBackButton(c);
	for (int k = 0; k < 20; k++) {
		int x = KX + (k % 4) * (KW + KGX), y = KY + (k / 4) * (KH + KGY);
		bool op = (k % 4) == 3 || k < 3;
		u16 bg = k == 19 ? T.accent : (op ? T.surface2 : T.surface);
		cvRRect(c, x, y, KW, KH, 10, bg);
		if (k == 18) cvIcon(c, IC_BKSP_20, x + KW / 2 - 10, y + 3, T.text);
		else cvTextC(c, FONT_TITLE, x + KW / 2, y + 1, k == 19 ? T.onAccent : (op ? T.accent : T.text), KEYS[k]);
		if (k == s_cur) cvRRectBorder(c, x - 3, y - 3, KW + 6, KH + 6, 12, 2, uiSelColor());
	}
	static const Hint h[] = {{KEY_START, "="}, {KEY_B, "Voltar"}};
	uiHints(c, h, 2);
}

const App app_calc = {"Calculadora", NULL, NULL, cFrame, cDrawTop, cDrawBot};

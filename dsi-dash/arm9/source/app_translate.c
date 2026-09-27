// DSi Dash — Tradutor (MyMemory, sem chave)
#include "common.h"

typedef struct Lang {
	const char* code;
	const char* name;
} Lang;

static const Lang LANGS[] = {
	{"pt-BR", "Portugu\xC3\xAAs"}, {"en", "Ingl\xC3\xAAs"}, {"es", "Espanhol"}, {"fr", "Franc\xC3\xAAs"},
	{"de", "Alem\xC3\xA3o"}, {"it", "Italiano"}, {"nl", "Holand\xC3\xAAs"}, {"la", "Latim"},
};
#define NLANGS ARRAY_SIZE(LANGS)

static int s_from = 0, s_to = 1;
static char s_src[500], s_dst[1200];
static NetJob s_job;
static int s_focus;  // 0 = de, 1 = trocar, 2 = para, 3 = texto

static void trJob(NetJob* j) {
	char q[1500], url[1800];
	urlEncode(s_src, q, sizeof(q));
	snprintf(url, sizeof(url), "https://api.mymemory.translated.net/get?q=%s&langpair=%s%%7C%s", q, LANGS[s_from].code, LANGS[s_to].code);
	HttpResp r;
	if (httpGet(j, url, &r, 65536) < 0) {
		j->result = -1;
		return;
	}
	const char* rd = strstr(r.body, "\"responseData\"");
	if (!rd || !jsonStr(rd, "translatedText", s_dst, sizeof(s_dst))) {
		snprintf(j->err, sizeof(j->err), "Resposta inv\xC3\xA1lida do tradutor");
		j->result = -2;
	}
	httpFree(&r);
}

static void translate(void) {
	if (!s_src[0] || jobBusy(&s_job)) return;
	s_dst[0] = 0;
	s_job.run = trJob;
	netSubmit(&s_job);
	gfxInvalidate(GFX_BOTH);
}

static void onText(const char* t) {
	if (!t) return;
	snprintf(s_src, sizeof(s_src), "%s", t);
	translate();
}

static void swap(void) {
	int t = s_from;
	s_from = s_to;
	s_to = t;
	if (s_dst[0]) {
		snprintf(s_src, sizeof(s_src), "%s", s_dst);
		translate();
	}
	sndMove();
	gfxInvalidate(GFX_BOTH);
}

static void tFrame(void) {
	if (s_job.state == JOB_DONE) {
		s_job.state = JOB_IDLE;
		if (s_job.result != 0) uiToast(s_job.err[0] ? s_job.err : "Falha ao traduzir");
		gfxInvalidate(GFX_BOTH);
	}
	if (g_in.down & KEY_B) {
		sndBack();
		appHome();
		return;
	}
	if (g_in.rep & KEY_LEFT) s_focus = (s_focus + 3) % 4;
	if (g_in.rep & KEY_RIGHT) s_focus = (s_focus + 1) % 4;
	if (g_in.down & KEY_DOWN) s_focus = 3;
	if (g_in.down & KEY_UP && s_focus == 3) s_focus = 1;
	bool a = g_in.down & KEY_A;
	if (g_in.down & KEY_X) swap();
	if (g_in.tap) {
		if (inRect(g_in.sx, g_in.sy, 8, 30, 96, 30)) s_focus = 0, a = true;
		else if (inRect(g_in.sx, g_in.sy, 112, 30, 32, 30)) s_focus = 1, a = true;
		else if (inRect(g_in.sx, g_in.sy, 152, 30, 96, 30)) s_focus = 2, a = true;
		else if (inRect(g_in.sx, g_in.sy, 8, 72, SCR_W - 16, 90)) s_focus = 3, a = true;
	}
	if (a) {
		if (s_focus == 0) s_from = (s_from + 1) % NLANGS, translate();
		else if (s_focus == 2) s_to = (s_to + 1) % NLANGS, translate();
		else if (s_focus == 1) swap();
		else kbdOpen("Texto para traduzir", s_src, 480, onText);
		sndMove();
	}
	gfxInvalidate(GFX_BOT);
}

static void tDrawTop(Canvas* c) {
	cvClear(c, T.bg);
	uiStatusBar(c, false);
	uiHeader(c, IC_TRANSLATE_20, "Tradutor");
	cvRRect(c, 8, STATUS_H + 32, SCR_W - 16, 50, 10, T.surface);
	cvText(c, FONT_SMALL, 16, STATUS_H + 34, T.text2, LANGS[s_from].name);
	cvTextWrap(c, FONT_BODY, 16, STATUS_H + 50, SCR_W - 32, 2, T.text, s_src[0] ? s_src : "\xE2\x80\x94");
	cvRRect(c, 8, STATUS_H + 88, SCR_W - 16, SCR_H - STATUS_H - 94, 10, T.surface);
	cvText(c, FONT_SMALL, 16, STATUS_H + 90, T.accent, LANGS[s_to].name);
	if (jobBusy(&s_job)) uiSpinner(c, SCR_W / 2, STATUS_H + 130, 10, T.accent);
	else cvTextWrap(c, FONT_TITLE, 16, STATUS_H + 106, SCR_W - 32, 3, T.text, s_dst[0] ? s_dst : "");
}

static void tDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	uiBackButton(c);
	cvTextC(c, FONT_TITLE, SCR_W / 2, 3, T.text, "Tradutor");
	uiButton(c, 8, 30, 96, 30, LANGS[s_from].name, false, s_focus == 0);
	cvCircle(c, 128, 45, 15, T.surface);
	cvIcon(c, IC_SWAPCAM_20, 118, 35, T.accent);
	if (s_focus == 1) cvRing(c, 128, 45, 19, 3, uiSelColor());
	uiButton(c, 152, 30, 96, 30, LANGS[s_to].name, false, s_focus == 2);
	cvRRect(c, 8, 72, SCR_W - 16, 90, 12, T.surface);
	cvTextWrap(c, FONT_BODY, 18, 80, SCR_W - 36, 4, s_src[0] ? T.text : T.text2, s_src[0] ? s_src : "Toque aqui para digitar o texto");
	if (s_focus == 3) uiSelBorder(c, 8, 72, SCR_W - 16, 90, 12);
	static const Hint h[] = {{KEY_X, "Inverter"}, {KEY_A, "Escolher"}, {KEY_B, "Voltar"}};
	uiHints(c, h, 3);
}

const App app_translate = {"Tradutor", NULL, NULL, tFrame, tDrawTop, tDrawBot};

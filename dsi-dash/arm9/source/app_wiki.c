// DSi Dash — Wikipedia: busca, previa do artigo e leitura no navegador
#include "common.h"

#define MAX_RES 15

static char s_query[128];
static char* s_title[MAX_RES];
static char* s_descr[MAX_RES];
static int s_count;
static ListView s_lv;
static NetJob s_search, s_sumJob;
static char s_err[96];

// previa (resumo) do item selecionado
static char s_sumFor[160];
static char s_sumTitle[160];
static char s_sumText[1400];
static char s_sumReq[160];
static int s_sumDelay;
static bool s_random;

static void freeRes(void) {
	for (int i = 0; i < s_count; i++) {
		free(s_title[i]);
		free(s_descr[i]);
	}
	s_count = 0;
}

// le o n-esimo array de strings do JSON do opensearch: ["q",[titulos],[descricoes],[urls]]
static int readArray(const char* p, char** out, int max) {
	int n = 0;
	while (*p && *p != '[') p++;
	if (*p != '[') return 0;
	p++;
	while (*p && *p != ']' && n < max) {
		while (*p == ' ' || *p == ',') p++;
		if (*p != '"') break;
		p++;
		const char* s = p;
		while (*p && *p != '"') {
			if (*p == '\\' && p[1]) p++;
			p++;
		}
		char tmp[400];
		jsonUnescape(s, p - s, tmp, sizeof(tmp));
		out[n++] = strdup(tmp);
		if (*p == '"') p++;
	}
	return n;
}

static char* s_nt[MAX_RES];
static char* s_nd[MAX_RES];
static int s_nCount;

static void searchJob(NetJob* j) {
	char q[300], url[512];
	urlEncode(s_query, q, sizeof(q));
	snprintf(url, sizeof(url), "https://%s.wikipedia.org/w/api.php?action=opensearch&search=%s&limit=%d&namespace=0&format=json", g_set.wikiLang, q, MAX_RES);
	HttpResp r;
	s_nCount = 0;
	if (httpGet(j, url, &r, 65536) < 0) {
		j->result = -1;
		return;
	}
	// pula o primeiro elemento (a consulta)
	const char* p = strchr(r.body, '[');
	if (p) p = strchr(p + 1, '[');
	if (p) {
		s_nCount = readArray(p, s_nt, MAX_RES);
		const char* p2 = strchr(p, ']');
		int nd = 0;
		if (p2) nd = readArray(p2 + 1, s_nd, MAX_RES);
		for (int i = nd; i < s_nCount; i++) s_nd[i] = strdup("");
		for (int i = s_nCount; i < nd; i++) free(s_nd[i]);
	}
	httpFree(&r);
	j->result = 0;
}

static void summaryJob(NetJob* j) {
	char t[300], url[512];
	s_sumText[0] = 0;
	if (s_random) {
		snprintf(url, sizeof(url), "https://%s.wikipedia.org/api/rest_v1/page/random/summary", g_set.wikiLang);
	} else {
		// titulos usam _ no lugar de espaco
		char tt[160];
		snprintf(tt, sizeof(tt), "%s", s_sumReq);
		for (char* c = tt; *c; c++)
			if (*c == ' ') *c = '_';
		urlEncode(tt, t, sizeof(t));
		snprintf(url, sizeof(url), "https://%s.wikipedia.org/api/rest_v1/page/summary/%s", g_set.wikiLang, t);
	}
	HttpResp r;
	if (httpGet(j, url, &r, 65536) < 0) {
		j->result = -1;
		return;
	}
	if (!jsonStr(r.body, "title", s_sumTitle, sizeof(s_sumTitle))) snprintf(s_sumTitle, sizeof(s_sumTitle), "%s", s_sumReq);
	if (!jsonStr(r.body, "extract", s_sumText, sizeof(s_sumText))) snprintf(s_sumText, sizeof(s_sumText), "(sem resumo)");
	httpFree(&r);
	snprintf(s_sumFor, sizeof(s_sumFor), "%s", s_random ? s_sumTitle : s_sumReq);
	j->result = 0;
}

static void startSearch(const char* q) {
	if (!q || !q[0]) return;
	snprintf(s_query, sizeof(s_query), "%s", q);
	if (jobBusy(&s_search)) s_search.cancel = true;
	s_err[0] = 0;
	s_search.run = searchJob;
	netSubmit(&s_search);
	gfxInvalidate(GFX_BOTH);
}

static void openArticle(const char* title) {
	char t[300], tt[200], url[600];
	snprintf(tt, sizeof(tt), "%s", title);
	for (char* c = tt; *c; c++)
		if (*c == ' ') *c = '_';
	urlEncode(tt, t, sizeof(t));
	// action=render devolve so o conteudo do artigo (sem menus), bem mais leve
	snprintf(url, sizeof(url), "https://%s.m.wikipedia.org/w/index.php?title=%s&action=render", g_set.wikiLang, t);
	browserOpenUrl(url);
}

static void drawRow(Canvas* c, ListView* lv, int i, int x, int y, int w, int h, bool sel) {
	cvRRect(c, x + 4, y + 2, w - 8, h - 4, 8, sel ? T.surface2 : T.surface);
	cvTextFit(c, FONT_BODY, x + 12, y + 3, w - 24, T.text, s_title[i]);
	cvTextFit(c, FONT_SMALL, x + 12, y + 20, w - 24, T.text2, s_descr[i][0] ? s_descr[i] : "Wikip\xC3\xA9" "dia");
	if (sel) cvRRectBorder(c, x + 2, y, w - 4, h, 10, 2, uiSelColor());
}

static void wkEnter(void) { lvInit(&s_lv, 0, 58, SCR_W, HINTS_Y - 60, 38, s_count, drawRow); }

static void requestSummary(bool random) {
	if (jobBusy(&s_sumJob)) return;
	s_random = random;
	if (!random) {
		if (!s_count) return;
		snprintf(s_sumReq, sizeof(s_sumReq), "%s", s_title[s_lv.sel]);
		if (!strcmp(s_sumReq, s_sumFor)) return;
	}
	s_sumJob.run = summaryJob;
	netSubmit(&s_sumJob);
}

static void wkFrame(void) {
	if (s_search.state == JOB_DONE) {
		s_search.state = JOB_IDLE;
		if (s_search.result == 0) {
			freeRes();
			for (int i = 0; i < s_nCount; i++) {
				s_title[i] = s_nt[i];
				s_descr[i] = s_nd[i];
			}
			s_count = s_nCount;
			s_nCount = 0;
			lvInit(&s_lv, 0, 58, SCR_W, HINTS_Y - 60, 38, s_count, drawRow);
			if (!s_count) snprintf(s_err, sizeof(s_err), "Nada encontrado para \"%s\"", s_query);
			s_sumDelay = 20;
		} else {
			snprintf(s_err, sizeof(s_err), "%s", s_search.err[0] ? s_search.err : "Falha na busca");
		}
		gfxInvalidate(GFX_BOTH);
	}
	if (s_sumJob.state == JOB_DONE) {
		s_sumJob.state = JOB_IDLE;
		gfxInvalidate(GFX_TOP);
	}
	if (g_in.down & KEY_B) {
		sndBack();
		appHome();
		return;
	}
	if ((g_in.down & KEY_X) || tapIn(8, 26, SCR_W - 16, 28)) kbdOpen("Pesquisar na Wikip\xC3\xA9" "dia", s_query, 100, startSearch);
	if (g_in.down & KEY_Y) requestSummary(true);
	int old = s_lv.sel;
	int act = lvUpdate(&s_lv);
	if (old != s_lv.sel) {
		sndMove();
		s_sumDelay = 25;  // espera parar de rolar antes de buscar o resumo
	}
	if (act >= 0 && act < s_count) openArticle(s_title[act]);
	if ((g_in.down & KEY_A) && !s_count && s_sumFor[0]) openArticle(s_sumFor);
	if (s_sumDelay > 0 && --s_sumDelay == 0) requestSummary(false);
	gfxInvalidate(GFX_BOT);
	if ((jobBusy(&s_sumJob) || jobBusy(&s_search)) && (g_frame % 6) == 0) gfxInvalidate(GFX_TOP);
}

static void wkDrawTop(Canvas* c) {
	cvClear(c, T.bg);
	uiStatusBar(c, false);
	uiHeader(c, IC_WIKI_20, "Wikip\xC3\xA9" "dia");
	if (jobBusy(&s_sumJob) || jobBusy(&s_search)) uiSpinner(c, SCR_W - 20, STATUS_H + 14, 7, T.accent);
	const char* want = s_count ? s_title[s_lv.sel] : s_sumFor;
	if (s_sumFor[0] && want && !strcmp(want, s_sumFor)) {
		int y = STATUS_H + 34;
		y += cvTextWrap(c, FONT_TITLE, 10, y, SCR_W - 20, 2, T.text, s_sumTitle);
		cvTextWrap(c, FONT_SMALL, 10, y + 4, SCR_W - 20, (SCR_H - y - 22) / fontHeight(FONT_SMALL), T.text2, s_sumText);
		cvTextC(c, FONT_SMALL, SCR_W / 2, SCR_H - 16, T.accent, "A: ler o artigo completo");
	} else {
		uiEmpty(c, IC_WIKI_40, "A enciclop\xC3\xA9" "dia livre", "Toque na barra de busca (ou X) para pesquisar. Y abre um artigo aleat\xC3\xB3rio.");
	}
}

static void wkDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	uiBackButton(c);
	cvTextC(c, FONT_TITLE, SCR_W / 2, 3, T.text, "Pesquisar");
	cvRRect(c, 8, 28, SCR_W - 16, 26, 13, T.surface);
	cvRRectBorder(c, 8, 28, SCR_W - 16, 26, 13, 1, T.line);
	cvIcon(c, IC_SEARCH_14, 18, 34, T.text2);
	cvTextFit(c, FONT_BODY, 38, 31, SCR_W - 60, s_query[0] ? T.text : T.text2, s_query[0] ? s_query : "Buscar artigos\xE2\x80\xA6");
	if (jobBusy(&s_search)) uiSpinner(c, SCR_W / 2, 110, 10, T.accent);
	else if (s_count) lvDraw(c, &s_lv);
	else if (s_err[0]) cvTextC(c, FONT_SMALL, SCR_W / 2, 100, T.text2, s_err);
	static const Hint h[] = {{KEY_Y, "Aleat\xC3\xB3rio"}, {KEY_X, "Buscar"}, {KEY_A, "Ler"}, {KEY_B, "Voltar"}};
	uiHints(c, h, 4);
}

const App app_wiki = {"Wikip\xC3\xA9" "dia", wkEnter, NULL, wkFrame, wkDrawTop, wkDrawBot};

// DSi Dash — Navegador (modo leitura). A pagina corre pelas duas telas como um livro:
// tela de cima = parte de cima, tela de baixo = continuacao + barra de ferramentas.
#include "common.h"
#include "html.h"
#include "image.h"
#include <sys/stat.h>

#define TOP_BAR 16
#define TOP_VIEW (SCR_H - TOP_BAR)   // 176
#define TOOL_H 24
#define BOT_VIEW (SCR_H - TOOL_H)    // 168
#define VIEW_H (TOP_VIEW + BOT_VIEW)
#define HIST_MAX 24
#define BM_MAX 40

typedef struct HistEnt {
	char* url;
	int scroll;
} HistEnt;

static Doc* s_doc;
static int s_scroll, s_vel;
static int s_focus = -1;
static bool s_dragging;
static int s_grab;
static HistEnt s_hist[HIST_MAX];
static int s_histLen, s_histPos = -1;
static int s_restoreScroll = -1;

// carregamento
static NetJob s_job;
static struct {
	char url[1024];
	char* post;
	int postLen;
	bool addHist;
} s_req;
static Doc* s_newDoc;
static char* s_raw;          // arquivo baixado (nao-HTML)
static int s_rawLen;
static char s_rawName[96];
static char* s_lastRaw;      // arquivo da pagina atual (para salvar)
static int s_lastRawLen;
static char s_lastRawName[96];
static char s_status[64];

// fonte do HTML (para refazer o layout quando as imagens chegam)
static char* s_src;
static int s_srcLen;
static char s_srcCtype[96];
static char* s_newSrc;
static int s_newSrcLen;
static char s_newSrcCtype[96];

#define IMG_MAX 12
typedef struct ImgEnt {
	char* url;
	u16* px;
	int w, h;
	volatile u8 state;  // 0 pendente, 1 ok, 2 erro
} ImgEnt;
static ImgEnt s_img[IMG_MAX];
static int s_nImg;
static NetJob s_imgJob;
static Doc* volatile s_relay;   // documento refeito com imagens (thread de rede -> principal)
static char s_relayUrl[1024];

// favoritos
static char* s_bmUrl[BM_MAX];
static char* s_bmTitle[BM_MAX];
static int s_bmCount;
static bool s_bmLoaded;
static char s_pendingOpen[1024];
static bool s_openPending;
static int s_returnApp = APP_HOME, s_returnPos = -1;
static bool s_markReturn;

static void navigate(const char* url, const char* post, int postLen, bool addHist);

// ---------------------------------------------------------------------------
// favoritos
// ---------------------------------------------------------------------------
static void bmLoad(void) {
	if (s_bmLoaded) return;
	s_bmLoaded = true;
	if (!sysHasStorage()) return;
	char p[96];
	sysDataPath(p, sizeof(p), "bookmarks.txt");
	FILE* f = fopen(p, "r");
	if (!f) return;
	char line[1200];
	while (s_bmCount < BM_MAX && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		char* tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = 0;
		s_bmUrl[s_bmCount] = strdup(line);
		s_bmTitle[s_bmCount] = strdup(tab + 1);
		s_bmCount++;
	}
	fclose(f);
}

static void bmSave(void) {
	if (!sysHasStorage()) return;
	char p[96];
	sysDataPath(p, sizeof(p), "bookmarks.txt");
	FILE* f = fopen(p, "w");
	if (!f) return;
	for (int i = 0; i < s_bmCount; i++) fprintf(f, "%s\t%s\n", s_bmUrl[i], s_bmTitle[i]);
	fclose(f);
}

static void bmAdd(const char* url, const char* title) {
	bmLoad();
	for (int i = 0; i < s_bmCount; i++)
		if (!strcmp(s_bmUrl[i], url)) {
			uiToast("J\xC3\xA1 est\xC3\xA1 nos favoritos");
			return;
		}
	if (s_bmCount >= BM_MAX) {
		uiToast("Favoritos cheios");
		return;
	}
	s_bmUrl[s_bmCount] = strdup(url);
	char t[128];
	snprintf(t, sizeof(t), "%s", title && title[0] ? title : url);
	for (char* c = t; *c; c++)
		if (*c == '\t') *c = ' ';
	s_bmTitle[s_bmCount] = strdup(t);
	s_bmCount++;
	bmSave();
	uiToast(sysHasStorage() ? "Adicionado aos favoritos" : "Adicionado (sem cart\xC3\xA3o SD: n\xC3\xA3o ser\xC3\xA1 salvo)");
}

static void bmDel(int i) {
	if (i < 0 || i >= s_bmCount) return;
	free(s_bmUrl[i]);
	free(s_bmTitle[i]);
	for (int k = i; k < s_bmCount - 1; k++) {
		s_bmUrl[k] = s_bmUrl[k + 1];
		s_bmTitle[k] = s_bmTitle[k + 1];
	}
	s_bmCount--;
	bmSave();
}

// ---------------------------------------------------------------------------
// paginas internas (HTML gerado, desenhado pelo proprio motor)
// ---------------------------------------------------------------------------
static int htmlEsc(char* o, int sz, const char* s) {
	int n = 0;
	for (; *s && n < sz - 7; s++) {
		if (*s == '<') n += snprintf(o + n, sz - n, "&lt;");
		else if (*s == '>') n += snprintf(o + n, sz - n, "&gt;");
		else if (*s == '&') n += snprintf(o + n, sz - n, "&amp;");
		else if (*s == '"') n += snprintf(o + n, sz - n, "&quot;");
		else o[n++] = *s;
	}
	o[n] = 0;
	return n;
}

static char* buildHome(int* len) {
	bmLoad();
	int cap = 16384;
	char* h = (char*)malloc(cap);
	if (!h) return NULL;
	int n = 0;
	n += snprintf(h + n, cap - n,
		"<title>In\xC3\xAD" "cio</title><h1>Navegador</h1>"
		"<form action=\"%s\" method=\"get\"><input type=\"text\" name=\"q\" size=\"26\"> <input type=\"submit\" value=\"Buscar\"></form>"
		"<p><small>%s</small></p>",
		tlsAvailable() ? "https://html.duckduckgo.com/html/" : "http://frogfind.com/",
		tlsAvailable() ? "Busca pelo DuckDuckGo. Toque no campo para digitar." : "Busca pelo FrogFind (funciona sem HTTPS). Toque no campo para digitar.");
	n += snprintf(h + n, cap - n, "<h2>Favoritos</h2>");
	if (!s_bmCount) n += snprintf(h + n, cap - n, "<p><small>Nenhum favorito ainda. Abra uma p\xC3\xA1gina e toque na estrela.</small></p>");
	else {
		n += snprintf(h + n, cap - n, "<ul>");
		for (int i = 0; i < s_bmCount && n < cap - 2600; i++) {
			char t[260], u[1100];
			htmlEsc(t, sizeof(t), s_bmTitle[i]);
			htmlEsc(u, sizeof(u), s_bmUrl[i]);
			n += snprintf(h + n, cap - n, "<li><a href=\"%s\">%s</a> <small><a href=\"dsidash:delbm?%d\">remover</a></small></li>", u, t, i);
		}
		n += snprintf(h + n, cap - n, "</ul>");
	}
	n += snprintf(h + n, cap - n,
		"<h2>Sites leves</h2><ul>"
		"<li><a href=\"http://68k.news/index.php?loc=BR\">68k.news</a> <small>Google Not\xC3\xAD" "cias em HTML simples</small></li>"
		"<li><a href=\"https://pt.m.wikipedia.org/\">Wikip\xC3\xA9" "dia</a> <small>(HTTPS)</small></li>"
		"<li><a href=\"https://lite.cnn.com/\">CNN Lite</a> <small>(HTTPS, ingl\xC3\xAAs)</small></li>"
		"<li><a href=\"https://text.npr.org/\">NPR Text</a> <small>(HTTPS, ingl\xC3\xAAs)</small></li>"
		"<li><a href=\"https://news.ycombinator.com/\">Hacker News</a> <small>(HTTPS)</small></li>"
		"<li><a href=\"http://frogfind.com/\">FrogFind</a> <small>busca para computadores antigos</small></li>"
		"<li><a href=\"http://info.cern.ch/\">info.cern.ch</a> <small>o primeiro site da web</small></li>"
		"</ul>"
		"<h2>Dicas</h2><p><small>Arraste na tela de baixo para rolar. \xE2\x86\x90/\xE2\x86\x92 escolhem links, A abre, B volta, X digita um endere\xC3\xA7o, Y adiciona aos favoritos, L/R pulam p\xC3\xA1ginas.</small></p>");
	*len = n;
	return h;
}

static char* buildMessage(int* len, const char* title, const char* msg, const char* url) {
	int cap = 6144;
	char* h = (char*)malloc(cap);
	if (!h) return NULL;
	char t[200], m[400], u[1100];
	htmlEsc(t, sizeof(t), title);
	htmlEsc(m, sizeof(m), msg);
	htmlEsc(u, sizeof(u), url ? url : "");
	int n = snprintf(h, cap, "<title>%s</title><h1>%s</h1><p>%s</p>", t, t, m);
	if (url && url[0]) n += snprintf(h + n, cap - n, "<p><small>%s</small></p><p><a href=\"%s\">Tentar de novo</a></p>", u, u);
	if (url && !strncasecmp(url, "https://", 8) && !tlsAvailable())
		n += snprintf(h + n, cap - n, "<p>Este site usa HTTPS, que ainda n\xC3\xA3o est\xC3\xA1 dispon\xC3\xADvel. Tente a vers\xC3\xA3o <a href=\"http://%s\">HTTP</a>.</p>", u + 8);
	n += snprintf(h + n, cap - n, "<p><a href=\"about:home\">P\xC3\xA1gina inicial</a></p>");
	*len = n;
	return h;
}

// ---------------------------------------------------------------------------
// carregamento (thread de rede)
// ---------------------------------------------------------------------------
static void fileNameFromUrl(const char* url, char* out, int sz) {
	const char* q = strchr(url, '?');
	int end = q ? (int)(q - url) : (int)strlen(url);
	int st = end;
	while (st > 0 && url[st - 1] != '/') st--;
	int n = MIN(end - st, sz - 1);
	if (n <= 0) {
		snprintf(out, sz, "download.bin");
		return;
	}
	memcpy(out, url + st, n);
	out[n] = 0;
	for (char* c = out; *c; c++)
		if (strchr("\\:*?\"<>|%", *c)) *c = '_';
}

static void loadJob(NetJob* j) {
	s_newDoc = NULL;
	s_raw = NULL;
	int len = 0;
	if (!strcmp(s_req.url, "about:home") || !strcmp(s_req.url, "about:blank")) {
		char* h = buildHome(&len);
		if (h) {
			s_newDoc = docParseHtml(h, len, "about:home", NULL);
			free(h);
		}
		return;
	}
	if (!strncmp(s_req.url, "file:", 5)) {
		// arquivo local do cartao SD
		const char* path = s_req.url + 5;
		FILE* f = fopen(path, "rb");
		if (!f) {
			char* h = buildMessage(&len, "Arquivo nÃ£o encontrado", path, NULL);
			if (h) {
				s_newDoc = docParseHtml(h, len, s_req.url, NULL);
				free(h);
			}
			return;
		}
		int cap = sysIsDSi() ? 2 * 1024 * 1024 : 256 * 1024;
		char* b = (char*)malloc(cap + 1);
		int n = b ? fread(b, 1, cap, f) : 0;
		fclose(f);
		if (b) {
			b[n] = 0;
			const char* ext = strrchr(path, '.');
			bool isHtml = ext && (!strcasecmp(ext, ".html") || !strcasecmp(ext, ".htm"));
			s_newDoc = isHtml ? docParseHtml(b, n, s_req.url, NULL) : docFromText(b, n, s_req.url);
			if (s_newDoc) {
				const char* nm = strrchr(path, '/');
				snprintf(s_newDoc->title, sizeof(s_newDoc->title), "%s", nm ? nm + 1 : path);
			}
			free(b);
		}
		return;
	}
	HttpReq rq = {0};
	if (s_req.post) {
		rq.method = "POST";
		rq.body = s_req.post;
		rq.bodyLen = s_req.postLen;
		rq.contentType = "application/x-www-form-urlencoded";
	}
	HttpResp r;
	int rc = httpRequest(j, s_req.url, &rq, &r);
	if (rc < 0) {
		if (j->cancel) return;
		char* h = buildMessage(&len, "N\xC3\xA3o foi poss\xC3\xADvel abrir a p\xC3\xA1gina", j->err, s_req.url);
		if (h) {
			s_newDoc = docParseHtml(h, len, s_req.url, NULL);
			free(h);
		}
		return;
	}
	const char* ct = r.ctype;
	bool html = strcasestr(ct, "html") != NULL || (!ct[0] && r.len > 0 && strchr("< \n\r\t", r.body[0]) && strcasestr(r.body, "<html"));
	bool text = !html && (strncasecmp(ct, "text/", 5) == 0 || strcasestr(ct, "json") || strcasestr(ct, "xml") || strcasestr(ct, "javascript"));
	if (html) {
		s_newDoc = docParseHtml(r.body, r.len, r.url, ct);
		s_newSrc = r.body;
		s_newSrcLen = r.len;
		snprintf(s_newSrcCtype, sizeof(s_newSrcCtype), "%s", ct);
		r.body = NULL;
		if (s_newDoc && r.status >= 400) {
			char t[64];
			snprintf(t, sizeof(t), "Erro %d \xE2\x80\x94 ", r.status);
			int tl = strlen(t);
			memmove(s_newDoc->title + tl, s_newDoc->title, MIN((int)strlen(s_newDoc->title) + 1, (int)sizeof(s_newDoc->title) - tl - 1));
			memcpy(s_newDoc->title, t, tl);
		}
	} else if (text) {
		s_newDoc = docFromText(r.body, r.len, r.url);
	} else {
		// arquivo: guarda para salvar no cartao
		s_raw = r.body;
		s_rawLen = r.len;
		r.body = NULL;
		fileNameFromUrl(r.url, s_rawName, sizeof(s_rawName));
		char msg[300];
		snprintf(msg, sizeof(msg), "Tipo: %s \xE2\x80\x94 tamanho: %d KB%s", ct[0] ? ct : "desconhecido", (s_rawLen + 1023) / 1024, r.truncated ? " (incompleto: arquivo grande demais)" : "");
		char* h = (char*)malloc(4096);
		if (h) {
			char nm[200], m2[400];
			htmlEsc(nm, sizeof(nm), s_rawName);
			htmlEsc(m2, sizeof(m2), msg);
			len = snprintf(h, 4096, "<title>%s</title><h1>%s</h1><p>%s</p><p><input type=\"submit\" value=\"Salvar no cart\xC3\xA3o SD\" name=\"dsidash-save\"></p><p><small>Os arquivos v\xC3\xA3o para a pasta Downloads do cart\xC3\xA3o.</small></p>", nm, nm, m2);
			s_newDoc = docParseHtml(h, len, r.url, NULL);
			free(h);
		}
	}
	httpFree(&r);
}

static bool imgLookup(const char* url, u16** px, int* w, int* h) {
	for (int i = 0; i < s_nImg; i++)
		if (s_img[i].state == 1 && !strcmp(s_img[i].url, url)) {
			*px = s_img[i].px;
			*w = s_img[i].w;
			*h = s_img[i].h;
			return true;
		}
	return false;
}

static void imgClear(void) {
	for (int i = 0; i < s_nImg; i++) {
		free(s_img[i].url);
		free(s_img[i].px);
	}
	memset(s_img, 0, sizeof(s_img));
	s_nImg = 0;
}

static Doc* relayout(void) {
	if (!s_src) return NULL;
	return docParseHtmlEx(s_src, s_srcLen, s_relayUrl, s_srcCtype, imgLookup);
}

static void imgJob(NetJob* j) {
	int maxW = SCR_W - 2 * DOC_MARGIN, maxH = sysIsDSi() ? 220 : 150;
	int done = 0;
	for (int i = 0; i < s_nImg && !j->cancel; i++) {
		if (s_img[i].state) continue;
		HttpResp r;
		u16* px = NULL;
		int w = 0, h = 0;
		if (httpGet(j, s_img[i].url, &r, sysIsDSi() ? 600 * 1024 : 200 * 1024) == 0) {
			if (r.status == 200 && !r.truncated) px = imgDecodeMem((const u8*)r.body, r.len, maxW, maxH, &w, &h);
			httpFree(&r);
		}
		// imagens minusculas (icones) nao valem o espaco
		if (px && (w < 24 || h < 24)) {
			free(px);
			px = NULL;
		}
		s_img[i].px = px;
		s_img[i].w = w;
		s_img[i].h = h;
		s_img[i].state = px ? 1 : 2;
		if (px) done++;
		// publica um layout novo a cada 2 imagens (se o anterior ja foi usado)
		if (px && (done % 2) == 1 && !s_relay && !j->cancel) s_relay = relayout();
	}
	if (done && !j->cancel) {
		for (int t = 0; t < 200 && s_relay; t++) threadSleep(20000);
		if (!s_relay) s_relay = relayout();
	}
}

static void imgStart(void) {
	if (!g_set.images || !s_doc || !s_src) return;
	int lim = sysIsDSi() ? IMG_MAX : 4;
	for (int i = 0; i < s_doc->nImgs && s_nImg < lim; i++) {
		const char* u = docStr(s_doc, s_doc->imgs[i].srcOff);
		if (strncasecmp(u, "http", 4)) continue;
		bool dup = false;
		for (int k = 0; k < s_nImg; k++)
			if (!strcmp(s_img[k].url, u)) dup = true;
		if (dup) continue;
		s_img[s_nImg].url = strdup(u);
		s_img[s_nImg].state = 0;
		s_nImg++;
	}
	if (s_nImg) {
		snprintf(s_relayUrl, sizeof(s_relayUrl), "%s", s_doc->url);
		s_imgJob.run = imgJob;
		netSubmit(&s_imgJob);
	}
}

static void navigate(const char* url, const char* post, int postLen, bool addHist) {
	if (jobBusy(&s_imgJob)) s_imgJob.cancel = true;
	if (jobBusy(&s_job)) {
		s_job.cancel = true;
		return;
	}
	// esquemas internos
	if (!strncmp(url, "dsidash:delbm?", 14)) {
		bmDel(atoi(url + 14));
		navigate("about:home", NULL, 0, false);
		return;
	}
	if (!strncasecmp(url, "mailto:", 7) || !strncasecmp(url, "tel:", 4) || !strncasecmp(url, "javascript:", 11)) {
		uiToast("Tipo de link n\xC3\xA3o suportado");
		return;
	}
	snprintf(s_req.url, sizeof(s_req.url), "%s", url);
	free(s_req.post);
	s_req.post = NULL;
	if (post) {
		s_req.post = (char*)malloc(postLen + 1);
		if (s_req.post) {
			memcpy(s_req.post, post, postLen);
			s_req.post[postLen] = 0;
			s_req.postLen = postLen;
		}
	}
	s_req.addHist = addHist;
	s_job.run = loadJob;
	netSubmit(&s_job);
	gfxInvalidate(GFX_BOTH);
}

static void pushHistory(const char* url) {
	if (s_histPos >= 0 && s_histPos < s_histLen && !strcmp(s_hist[s_histPos].url, url)) return;
	for (int i = s_histPos + 1; i < s_histLen; i++) free(s_hist[i].url);
	s_histLen = s_histPos + 1;
	if (s_histLen >= HIST_MAX) {
		free(s_hist[0].url);
		memmove(&s_hist[0], &s_hist[1], sizeof(HistEnt) * (HIST_MAX - 1));
		s_histLen--;
	}
	s_hist[s_histLen].url = strdup(url);
	s_hist[s_histLen].scroll = 0;
	s_histPos = s_histLen++;
}

static void onLoaded(void) {
	if (!s_newDoc) {
		if (!s_job.cancel) uiToast("Sem mem\xC3\xB3ria para esta p\xC3\xA1gina");
		return;
	}
	if (s_histPos >= 0 && s_histPos < s_histLen) s_hist[s_histPos].scroll = s_scroll;
	docFree(s_doc);
	s_doc = s_newDoc;
	s_newDoc = NULL;
	// imagens da pagina anterior: o job ja terminou (a fila e unica), pode liberar
	if (s_relay) {
		docFree(s_relay);
		s_relay = NULL;
	}
	imgClear();
	free(s_src);
	s_src = s_newSrc;
	s_srcLen = s_newSrcLen;
	snprintf(s_srcCtype, sizeof(s_srcCtype), "%s", s_newSrcCtype);
	s_newSrc = NULL;
	free(s_lastRaw);
	s_lastRaw = s_raw;
	s_lastRawLen = s_rawLen;
	snprintf(s_lastRawName, sizeof(s_lastRawName), "%s", s_rawName);
	s_raw = NULL;
	if (s_req.addHist) pushHistory(s_doc->url[0] ? s_doc->url : s_req.url);
	if (s_markReturn) {
		s_returnPos = s_histPos;
		s_markReturn = false;
	}
	else if (s_histPos >= 0) {
		free(s_hist[s_histPos].url);
		s_hist[s_histPos].url = strdup(s_doc->url[0] ? s_doc->url : s_req.url);
	}
	s_scroll = 0;
	if (s_restoreScroll >= 0) {
		s_scroll = s_restoreScroll;
		s_restoreScroll = -1;
	}
	s_vel = 0;
	s_focus = -1;
	gfxInvalidate(GFX_BOTH);
	imgStart();
}

static void goBack(void) {
	if (s_histPos <= 0 || (s_returnPos >= 0 && s_histPos <= s_returnPos)) {
		int to = s_returnPos >= 0 ? s_returnApp : APP_HOME;
		s_returnPos = -1;
		sndBack();
		appOpen(to);
		return;
	}
	if (s_histPos < s_histLen) s_hist[s_histPos].scroll = s_scroll;
	s_histPos--;
	s_restoreScroll = s_hist[s_histPos].scroll;
	sndBack();
	navigate(s_hist[s_histPos].url, NULL, 0, false);
}

static void goForward(void) {
	if (s_histPos + 1 >= s_histLen) return;
	s_hist[s_histPos].scroll = s_scroll;
	s_histPos++;
	s_restoreScroll = s_hist[s_histPos].scroll;
	navigate(s_hist[s_histPos].url, NULL, 0, false);
}

// ---------------------------------------------------------------------------
// entrada de endereco / formularios
// ---------------------------------------------------------------------------
static void onUrl(const char* text) {
	if (!text || !text[0]) return;
	char url[1024];
	while (*text == ' ') text++;
	if (strstr(text, "://") || !strncmp(text, "about:", 6)) {
		snprintf(url, sizeof(url), "%s", text);
	} else if (strchr(text, '.') && !strchr(text, ' ')) {
		snprintf(url, sizeof(url), "%s%s", tlsAvailable() ? "https://" : "http://", text);
	} else {
		char q[600];
		urlEncode(text, q, sizeof(q));
		if (tlsAvailable()) snprintf(url, sizeof(url), "https://html.duckduckgo.com/html/?q=%s", q);
		else snprintf(url, sizeof(url), "http://frogfind.com/?q=%s", q);
	}
	navigate(url, NULL, 0, true);
}

static int s_editField = -1;

static void submitForm(int form, int button) {
	if (!s_doc || form < 0 || form >= s_doc->nForms) return;
	char* qs = (char*)malloc(4096);
	if (!qs) return;
	bool post;
	int n = docBuildSubmit(s_doc, form, button, qs, 4096, &post);
	const char* action = docStr(s_doc, s_doc->forms[form].actionOff);
	if (post) {
		navigate(action, qs, n, true);
	} else {
		char* url = (char*)malloc(5200);
		if (url) {
			snprintf(url, 5200, "%s", action);
			char* qm = strchr(url, '?');
			if (qm) *qm = 0;
			int ul = strlen(url);
			snprintf(url + ul, 5200 - ul, "?%s", qs);
			navigate(url, NULL, 0, true);
			free(url);
		}
	}
	free(qs);
}

static void onFieldText(const char* text) {
	if (!text || !s_doc || s_editField < 0 || s_editField >= s_doc->nFields) return;
	DocField* f = &s_doc->fields[s_editField];
	free(f->value);
	f->value = strdup(text);
	gfxInvalidate(GFX_BOTH);
	// formulario de busca (um campo so): envia direto
	int texts = 0;
	for (int i = 0; i < s_doc->nFields; i++)
		if (s_doc->fields[i].form == f->form && (s_doc->fields[i].type == FT_TEXT || s_doc->fields[i].type == FT_PASSWORD || s_doc->fields[i].type == FT_TEXTAREA)) texts++;
	if (texts == 1 && f->type == FT_TEXT && f->form >= 0) submitForm(f->form, -1);
}

static void saveDownload(void) {
	if (!s_lastRaw) return;
	if (!sysHasStorage()) {
		uiToast("Sem cart\xC3\xA3o SD");
		return;
	}
	char dir[64], path[200];
	snprintf(dir, sizeof(dir), "%sDownloads", sysRoot());
	mkdir(dir, 0777);
	snprintf(path, sizeof(path), "%s/%s", dir, s_lastRawName);
	FILE* f = fopen(path, "wb");
	if (!f) {
		uiToast("Erro ao salvar");
		return;
	}
	size_t w = fwrite(s_lastRaw, 1, s_lastRawLen, f);
	fclose(f);
	char m[160];
	snprintf(m, sizeof(m), w == (size_t)s_lastRawLen ? "Salvo em Downloads/%s" : "Erro ao gravar %s", s_lastRawName);
	uiToast(m);
}

static void activate(int f) {
	if (!s_doc || f < 0 || f >= s_doc->nFocus) return;
	DocFocus* fo = &s_doc->focus[f];
	if (fo->kind == FK_LINK) {
		const char* h = docFocusHref(s_doc, f);
		if (h) {
			snprintf(s_pendingOpen, sizeof(s_pendingOpen), "%s", h);
			sndClick();
			navigate(s_pendingOpen, NULL, 0, true);
		}
		return;
	}
	DocField* fl = &s_doc->fields[fo->idx];
	switch (fl->type) {
		case FT_TEXT:
		case FT_PASSWORD:
		case FT_TEXTAREA:
			s_editField = fo->idx;
			kbdOpen(fl->type == FT_PASSWORD ? "Senha" : "Digite", fl->value, 500, onFieldText);
			break;
		case FT_CHECKBOX:
			fl->checked ^= 1;
			gfxInvalidate(GFX_BOTH);
			break;
		case FT_RADIO:
			for (int i = 0; i < s_doc->nFields; i++) {
				DocField* o = &s_doc->fields[i];
				if (o->type == FT_RADIO && o->form == fl->form && !strcmp(docStr(s_doc, o->nameOff), docStr(s_doc, fl->nameOff))) o->checked = 0;
			}
			fl->checked = 1;
			gfxInvalidate(GFX_BOTH);
			break;
		case FT_SUBMIT:
			sndClick();
			if (!strcmp(docStr(s_doc, fl->nameOff), "dsidash-save")) saveDownload();
			else submitForm(fl->form, fo->idx);
			break;
	}
}

// ---------------------------------------------------------------------------
// foco / rolagem
// ---------------------------------------------------------------------------
static int maxScroll(void) { return s_doc ? MAX(0, s_doc->height - VIEW_H) : 0; }

static void focusVisible(int f) {
	int y0, y1;
	docFocusBox(s_doc, f, &y0, &y1);
	if (y0 < s_scroll + 8) s_scroll = y0 - 8;
	if (y1 > s_scroll + VIEW_H - 8) s_scroll = y1 - VIEW_H + 8;
	s_scroll = CLAMP(s_scroll, 0, maxScroll());
}

static void moveFocus(int dir) {
	if (!s_doc || !s_doc->nFocus) return;
	int f = s_focus;
	if (f < 0 || f >= s_doc->nFocus) {
		// primeiro foco visivel
		for (int i = 0; i < s_doc->nFocus; i++) {
			int y0, y1;
			docFocusBox(s_doc, i, &y0, &y1);
			if (y0 >= s_scroll) {
				f = i;
				break;
			}
		}
		if (f < 0) f = 0;
	} else {
		f = CLAMP(f + dir, 0, s_doc->nFocus - 1);
	}
	s_focus = f;
	focusVisible(f);
	sndMove();
	gfxInvalidate(GFX_BOTH);
}

static void bEnter(void) {
	if (!s_doc && !jobBusy(&s_job) && !s_openPending) {
		const char* start = g_set.homepage[0] ? g_set.homepage : "about:home";
		if (!strncmp(start, "http://frogfind.com", 19)) start = "about:home";
		navigate(start, NULL, 0, true);
	}
	gfxInvalidate(GFX_BOTH);
}

void browserOpenUrl(const char* url) {
	int from = appCurrent();
	if (from != APP_BROWSER) {
		s_returnApp = from;
		s_markReturn = true;
		appOpen(APP_BROWSER);
	}
	if (jobBusy(&s_job)) s_job.cancel = true;
	s_restoreScroll = -1;
	snprintf(s_pendingOpen, sizeof(s_pendingOpen), "%s", url);
	s_openPending = true;
}

static int toolX(int i) { return 2 + i * 30; }  // botoes: voltar, avancar, [endereco], estrela, recarregar

static void bFrame(void) {
	if (s_job.state == JOB_DONE) {
		s_job.state = JOB_IDLE;
		if (!s_job.cancel) onLoaded();
		else {
			docFree(s_newDoc);
			s_newDoc = NULL;
			free(s_raw);
			s_raw = NULL;
			free(s_newSrc);
			s_newSrc = NULL;
		}
		gfxInvalidate(GFX_BOTH);
	}
	if (s_imgJob.state == JOB_DONE) {
		s_imgJob.state = JOB_IDLE;
		gfxInvalidate(GFX_TOP);
	}
	if (jobBusy(&s_imgJob) && (g_frame % 30) == 0) gfxInvalidate(GFX_TOP);
	if (s_relay) {
		Doc* nd = s_relay;
		s_relay = NULL;
		if (s_doc && !strcmp(nd->url, s_doc->url)) {
			docFree(s_doc);
			s_doc = nd;
			if (s_focus >= s_doc->nFocus) s_focus = -1;
			gfxInvalidate(GFX_BOTH);
		} else {
			docFree(nd);
		}
	}
	if (s_openPending && !jobBusy(&s_job)) {
		s_openPending = false;
		navigate(s_pendingOpen, NULL, 0, true);
	}
	bool loading = jobBusy(&s_job);
	if (loading && (g_frame % 10) == 0) gfxInvalidate(GFX_BOTH);

	if (g_in.down & KEY_B) {
		if (loading) {
			s_job.cancel = true;
			uiToast("Cancelado");
		} else goBack();
		return;
	}
	if (g_in.down & KEY_START) {
		sndBack();
		appHome();
		return;
	}
	if (g_in.down & KEY_X) kbdOpen("Endere\xC3\xA7o ou busca", s_doc && strncmp(s_doc->url, "about:", 6) ? s_doc->url : "", 900, onUrl);
	if (g_in.down & KEY_Y && s_doc && strncmp(s_doc->url, "about:", 6)) bmAdd(s_doc->url, s_doc->title);
	if (g_in.down & KEY_SELECT) navigate("about:home", NULL, 0, true);

	int old = s_scroll;
	if (g_in.rep & KEY_DOWN) s_scroll += 28;
	if (g_in.rep & KEY_UP) s_scroll -= 28;
	if (g_in.rep & KEY_R) s_scroll += VIEW_H - 40;
	if (g_in.rep & KEY_L) s_scroll -= VIEW_H - 40;
	if (g_in.rep & KEY_RIGHT) moveFocus(+1);
	if (g_in.rep & KEY_LEFT) moveFocus(-1);
	if (g_in.down & KEY_A) activate(s_focus);

	// toque: area da pagina na tela de baixo
	if (g_in.tDown && g_in.ty < BOT_VIEW) {
		s_dragging = true;
		s_grab = s_scroll;
		s_vel = 0;
	}
	if (s_dragging && g_in.touch && g_in.drag) {
		s_scroll = s_grab - (g_in.ty - g_in.sy);
		s_vel = -g_in.dy * 256;
	}
	if (s_dragging && !g_in.touch) {
		s_dragging = false;
		if (g_in.tap && s_doc) {
			int f = docHit(s_doc, g_in.sx, s_scroll + TOP_VIEW + g_in.sy);
			if (f >= 0) {
				s_focus = f;
				activate(f);
			}
		}
	}
	if (!s_dragging && s_vel) {
		s_scroll += s_vel / 256;
		s_vel = s_vel * 15 / 16;
		if (abs(s_vel) < 200) s_vel = 0;
	}
	// barra de ferramentas
	if (g_in.tap && g_in.sy >= BOT_VIEW) {
		int x = g_in.sx;
		if (x < toolX(1)) goBack();
		else if (x < toolX(2)) goForward();
		else if (x >= SCR_W - 30) {
			if (loading) s_job.cancel = true;
			else if (s_histPos >= 0) {
				s_restoreScroll = s_scroll;
				navigate(s_hist[s_histPos].url, NULL, 0, false);
			}
		} else if (x >= SCR_W - 60) {
			if (s_doc && strncmp(s_doc->url, "about:", 6)) bmAdd(s_doc->url, s_doc->title);
			else navigate("about:home", NULL, 0, true);
		} else {
			kbdOpen("Endere\xC3\xA7o ou busca", s_doc && strncmp(s_doc->url, "about:", 6) ? s_doc->url : "", 900, onUrl);
		}
	}
	s_scroll = CLAMP(s_scroll, 0, maxScroll());
	if (s_scroll != old) gfxInvalidate(GFX_BOTH);
	if (s_focus >= 0 && !loading && (g_frame & 1)) gfxInvalidate(GFX_BOTH);  // borda animada
}

static const char* hostOf(const char* url, char* out, int sz) {
	const char* p = strstr(url, "://");
	p = p ? p + 3 : url;
	int n = 0;
	while (p[n] && p[n] != '/' && p[n] != '?' && n < sz - 1) {
		out[n] = p[n];
		n++;
	}
	out[n] = 0;
	return out;
}

static void bDrawTop(Canvas* c) {
	cvClear(c, T.bg);
	// barra de titulo
	cvFill(c, 0, 0, SCR_W, TOP_BAR, T.surface);
	char tb[32];
	if (jobBusy(&s_imgJob)) {
		int ok = 0, fin = 0;
		for (int i = 0; i < s_nImg; i++) {
			if (s_img[i].state) fin++;
			if (s_img[i].state == 1) ok++;
		}
		snprintf(tb, sizeof(tb), "imagens %d/%d", fin, s_nImg);
		(void)ok;
	} else {
		sysFormatTime(tb, sizeof(tb));
	}
	int tw = textWidth(FONT_SMALL, tb);
	cvText(c, FONT_SMALL, SCR_W - tw - 4, 0, T.text2, tb);
	const char* title = s_doc ? s_doc->title : "Navegador";
	bool secure = s_doc && !strncasecmp(s_doc->url, "https://", 8);
	int tx = 4;
	if (secure) {
		cvIcon(c, IC_LOCK_14, 2, 1, T.ok);
		tx = 18;
	}
	cvTextFit(c, FONT_SMALL, tx, 0, SCR_W - tw - tx - 10, T.text, title);
	if (jobBusy(&s_job)) {
		int w = s_job.total > 0 ? SCR_W * s_job.progress / s_job.total : (int)((g_frame * 3) % SCR_W);
		if (s_job.total > 0) cvFill(c, 0, TOP_BAR - 2, CLAMP(w, 0, SCR_W), 2, T.accent);
		else cvFill(c, w, TOP_BAR - 2, 50, 2, T.accent);
	}
	if (s_doc) docDraw(c, s_doc, s_scroll, TOP_BAR, TOP_VIEW, s_focus);
	else if (jobBusy(&s_job)) {
		uiSpinner(c, SCR_W / 2, 96, 12, T.accent);
		cvTextC(c, FONT_BODY, SCR_W / 2, 118, T.text2, "Carregando\xE2\x80\xA6");
	}
	// barra de rolagem
	if (s_doc && s_doc->height > VIEW_H) {
		int th = MAX(10, VIEW_H * VIEW_H / s_doc->height);
		int ty = (VIEW_H - th) * s_scroll / MAX(1, maxScroll());
		if (ty < TOP_VIEW) cvRRectA(c, SCR_W - 3, TOP_BAR + ty, 2, MIN(th, TOP_VIEW - ty), 1, T.text2, 14);
	}
}

static void bDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	if (s_doc) docDraw(c, s_doc, s_scroll + TOP_VIEW, 0, BOT_VIEW, s_focus);
	if (s_doc && s_doc->height > VIEW_H) {
		int th = MAX(10, VIEW_H * VIEW_H / s_doc->height);
		int ty = (VIEW_H - th) * s_scroll / MAX(1, maxScroll()) - TOP_VIEW;
		if (ty + th > 0) cvRRectA(c, SCR_W - 3, MAX(0, ty), 2, MIN(th, th + ty), 1, T.text2, 14);
	}
	// barra de ferramentas
	int y = BOT_VIEW;
	cvFill(c, 0, y, SCR_W, TOOL_H, T.surface);
	cvHLine(c, 0, y, SCR_W, T.line);
	bool canBack = s_histPos > 0, canFwd = s_histPos + 1 < s_histLen;
	cvIcon(c, IC_BACK_20, toolX(0) + 4, y + 2, T.text);
	cvIconA(c, IC_FWD_20, toolX(1) + 4, y + 2, T.text, canFwd ? 32 : 8);
	(void)canBack;
	int ux = toolX(2), uw = SCR_W - 62 - ux;
	cvRRect(c, ux, y + 3, uw, TOOL_H - 6, (TOOL_H - 6) / 2, T.bg);
	char host[80];
	if (jobBusy(&s_job)) {
		char st[64];
		if (s_job.progress > 0) snprintf(st, sizeof(st), "Carregando\xE2\x80\xA6 %d KB", s_job.progress / 1024);
		else snprintf(st, sizeof(st), "Conectando\xE2\x80\xA6");
		cvTextFit(c, FONT_SMALL, ux + 8, y + 3, uw - 16, T.text2, st);
	} else if (s_doc) {
		cvTextFit(c, FONT_SMALL, ux + 8, y + 3, uw - 16, T.text2, !strncmp(s_doc->url, "about:", 6) ? "Digite um endere\xC3\xA7o ou busca" : hostOf(s_doc->url, host, sizeof(host)));
	}
	bool isAbout = !s_doc || !strncmp(s_doc->url, "about:", 6);
	cvIcon(c, isAbout ? IC_HOME_20 : IC_STAR_20, SCR_W - 56, y + 2, isAbout ? T.text : HEX(0xF5B400));
	if (jobBusy(&s_job)) cvIcon(c, IC_CLOSE_20, SCR_W - 26, y + 2, T.text);
	else cvIcon(c, IC_REFRESH_20, SCR_W - 26, y + 2, T.text);
	(void)s_status;
}

static void bLeave(void) {}

const App app_browser = {"Navegador", bEnter, bLeave, bFrame, bDrawTop, bDrawBot};

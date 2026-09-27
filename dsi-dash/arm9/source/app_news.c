// DSi Dash — Noticias (leitor de RSS/Atom)
#include "common.h"
#include "html.h"

#define MAX_ITEMS 60

typedef struct Feed {
	const char* name;
	const char* url;
} Feed;

static const Feed FEEDS[] = {
	{"BBC Brasil", "https://feeds.bbci.co.uk/portuguese/rss.xml"},
	{"G1", "https://g1.globo.com/rss/g1/"},
	{"Folha", "https://feeds.folha.uol.com.br/emcimadahora/rss091.xml"},
	{"UOL", "https://rss.uol.com.br/feed/noticias.xml"},
	{"Nintendo Life", "https://www.nintendolife.com/feeds/latest"},
	{"Hacker News", "https://hnrss.org/frontpage"},
	{"The Verge", "https://www.theverge.com/rss/index.xml"},
	{"Meu feed", NULL},
};
#define NFEEDS ARRAY_SIZE(FEEDS)

typedef struct Item {
	char* title;
	char* link;
	char* desc;
	time_t when;  // UTC, 0 = desconhecido
} Item;

static Item s_items[MAX_ITEMS], s_new[MAX_ITEMS];
static int s_count, s_newCount;
static int s_feed;
static NetJob s_job;
static ListView s_lv;
static char s_err[96];
static char s_feedTitle[80];
static bool s_loadedOnce;

static const char* feedUrl(int i) { return FEEDS[i].url ? FEEDS[i].url : g_set.newsFeed; }

static void freeItems(Item* it, int n) {
	for (int i = 0; i < n; i++) {
		free(it[i].title);
		free(it[i].link);
		free(it[i].desc);
		memset(&it[i], 0, sizeof(Item));
	}
}

// conteudo de <tag ...>...</tag> dentro de [p, end)
static const char* tagContent(const char* p, const char* end, const char* tag, int* len, const char** attrs) {
	int tl = strlen(tag);
	while (p < end) {
		const char* a = strstr(p, "<");
		if (!a || a >= end) return NULL;
		if (!strncasecmp(a + 1, tag, tl) && (a[1 + tl] == '>' || a[1 + tl] == ' ' || a[1 + tl] == '/' || a[1 + tl] == '\n')) {
			const char* gt = memchr(a, '>', end - a);
			if (!gt) return NULL;
			if (attrs) *attrs = a;
			if (gt[-1] == '/') {  // <tag ... />
				*len = 0;
				return gt;
			}
			const char* st = gt + 1;
			char close[24];
			snprintf(close, sizeof(close), "</%s", tag);
			const char* ce = strcasestr(st, close);
			if (!ce || ce > end) ce = end;
			*len = ce - st;
			return st;
		}
		p = a + 1;
	}
	return NULL;
}

static char* textOf(const char* s, int n, int max, bool latin1) {
	int cap = MIN(n, max) * 2 + 16;
	char* o = (char*)malloc(cap);
	if (!o) return NULL;
	htmlToText(s, MIN(n, max * 3), o, cap, latin1);
	if ((int)strlen(o) > max) {
		int k = max;
		while (k > 0 && ((u8)o[k] & 0xC0) == 0x80) k--;
		strcpy(o + k, "\xE2\x80\xA6");
	}
	return o;
}

static time_t parseDate(const char* s) {
	while (*s == ' ' || *s == '\n') s++;
	int y, mo, d, h = 0, mi = 0, se = 0;
	if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) >= 3 && y > 1990) {
		time_t t = sysDaysFromCivil(y, mo, d) * 86400L + h * 3600L + mi * 60L + se;
		const char* tz = strchr(s + 10, '+');
		if (!tz) {
			tz = strchr(s + 10, '-');
		}
		if (tz && (tz[0] == '+' || tz[0] == '-') && strlen(tz) >= 5) {
			int oh = atoi(tz + 1), om = atoi(tz + 4);
			long off = (oh * 3600L + om * 60L) * (tz[0] == '+' ? 1 : -1);
			t -= off;
		}
		return t;
	}
	// RFC 822: "Sat, 26 Sep 2026 21:40:00 -0300"
	time_t t = sysParseHttpDate(s);
	if (t == (time_t)-1) {
		char buf[64];
		snprintf(buf, sizeof(buf), "Xxx, %s", s);
		t = sysParseHttpDate(buf);
		if (t == (time_t)-1) return 0;
	}
	const char* tz = strrchr(s, ' ');
	if (tz && (tz[1] == '+' || tz[1] == '-') && isdigit((u8)tz[2])) {
		int v = atoi(tz + 2);
		long off = ((v / 100) * 3600L + (v % 100) * 60L) * (tz[1] == '+' ? 1 : -1);
		t -= off;
	}
	return t;
}

static void newsJob(NetJob* j) {
	s_newCount = 0;
	s_feedTitle[0] = 0;
	HttpResp r;
	if (httpGet(j, feedUrl(s_feed), &r, sysIsDSi() ? 640 * 1024 : 256 * 1024) < 0) {
		j->result = -1;
		return;
	}
	const char* b = r.body;
	const char* end = b + r.len;
	char head[256];
	int hl = MIN(r.len, 255);
	memcpy(head, b, hl);
	head[hl] = 0;
	bool latin1 = strcasestr(head, "ISO-8859-1") || strcasestr(r.ctype, "8859-1") || strcasestr(head, "windows-1252");
	bool atom = !strcasestr(head, "<rss") && strcasestr(b, "<entry");
	const char* itemTag = atom ? "entry" : "item";
	// titulo do canal
	int tl;
	const char* ch = tagContent(b, end, "title", &tl, NULL);
	if (ch) htmlToText(ch, MIN(tl, 200), s_feedTitle, sizeof(s_feedTitle), latin1);
	const char* p = b;
	while (s_newCount < MAX_ITEMS) {
		int il;
		const char* it = tagContent(p, end, itemTag, &il, NULL);
		if (!it) break;
		const char* ie = it + il;
		Item* o = &s_new[s_newCount];
		memset(o, 0, sizeof(*o));
		int n;
		const char* v = tagContent(it, ie, "title", &n, NULL);
		o->title = v ? textOf(v, n, 200, latin1) : strdup("(sem t\xC3\xADtulo)");
		const char* attrs = NULL;
		v = tagContent(it, ie, "link", &n, &attrs);
		if (atom && attrs) {
			const char* h = strcasestr(attrs, "href=\"");
			if (h && h < ie) {
				h += 6;
				const char* q = strchr(h, '"');
				if (q) o->link = strndup(h, q - h);
			}
		} else if (v && n > 0) {
			char tmp[1024];
			htmlToText(v, MIN(n, 1000), tmp, sizeof(tmp), latin1);
			o->link = strdup(tmp);
		}
		if (!o->link) {
			v = tagContent(it, ie, "guid", &n, NULL);
			if (v && n > 7 && !strncmp(v, "http", 4)) o->link = strndup(v, MIN(n, 1000));
		}
		v = tagContent(it, ie, "description", &n, NULL);
		if (!v) v = tagContent(it, ie, "summary", &n, NULL);
		if (!v) v = tagContent(it, ie, "content", &n, NULL);
		o->desc = v ? textOf(v, n, 700, latin1) : strdup("");
		v = tagContent(it, ie, "pubDate", &n, NULL);
		if (!v) v = tagContent(it, ie, "published", &n, NULL);
		if (!v) v = tagContent(it, ie, "updated", &n, NULL);
		if (!v) v = tagContent(it, ie, "dc:date", &n, NULL);
		if (v) {
			char tmp[64];
			snprintf(tmp, sizeof(tmp), "%.*s", MIN(n, 63), v);
			o->when = parseDate(tmp);
		}
		if (o->title && o->link) s_newCount++;
		else freeItems(o, 1);
		p = ie;
	}
	httpFree(&r);
	if (!s_newCount) {
		snprintf(j->err, sizeof(j->err), "Nenhuma not\xC3\xAD" "cia encontrada neste feed");
		j->result = -2;
		return;
	}
	j->result = 0;
}

static void load(void) {
	if (jobBusy(&s_job)) return;
	s_err[0] = 0;
	s_job.run = newsJob;
	netSubmit(&s_job);
	gfxInvalidate(GFX_BOTH);
}

static void fmtAge(time_t when, char* out, int sz) {
	if (!when) {
		out[0] = 0;
		return;
	}
	long d = (long)(sysUtcNow() - when);
	if (d < 0) d = 0;
	if (d < 60) snprintf(out, sz, "agora");
	else if (d < 3600) snprintf(out, sz, "h\xC3\xA1 %ld min", d / 60);
	else if (d < 86400) snprintf(out, sz, "h\xC3\xA1 %ld h", d / 3600);
	else {
		struct tm t;
		time_t lt = when + (sysNow() - sysUtcNow());
		gmtime_r(&lt, &t);
		snprintf(out, sz, "%02d/%02d", t.tm_mday, t.tm_mon + 1);
	}
}

static void drawRow(Canvas* c, ListView* lv, int i, int x, int y, int w, int h, bool sel) {
	const Item* it = &s_items[i];
	cvRRect(c, x + 4, y + 2, w - 8, h - 4, 8, sel ? T.surface2 : T.surface);
	cvTextWrap(c, FONT_TEXT, x + 12, y + 3, w - 24, 2, T.text, it->title);
	if (sel) cvRRectBorder(c, x + 2, y, w - 4, h, 10, 2, uiSelColor());
}

static void nEnter(void) {
	lvInit(&s_lv, 0, 26, SCR_W, HINTS_Y - 28, 42, s_count, drawRow);
	if (!s_loadedOnce) {
		s_loadedOnce = true;
		load();
	}
}

static void switchFeed(int d) {
	if (jobBusy(&s_job)) return;
	s_feed = (s_feed + d + NFEEDS) % NFEEDS;
	sndMove();
	load();
}

static void nFrame(void) {
	if (s_job.state == JOB_DONE) {
		s_job.state = JOB_IDLE;
		if (s_job.result == 0) {
			freeItems(s_items, s_count);
			memcpy(s_items, s_new, sizeof(Item) * s_newCount);
			s_count = s_newCount;
			memset(s_new, 0, sizeof(s_new));
			s_newCount = 0;
			lvInit(&s_lv, 0, 26, SCR_W, HINTS_Y - 28, 42, s_count, drawRow);
		} else {
			freeItems(s_new, s_newCount);
			s_newCount = 0;
			snprintf(s_err, sizeof(s_err), "%s", s_job.err[0] ? s_job.err : "Falha ao carregar");
		}
		gfxInvalidate(GFX_BOTH);
	}
	if (g_in.down & KEY_B) {
		if (jobBusy(&s_job)) s_job.cancel = true;
		sndBack();
		appHome();
		return;
	}
	if (g_in.down & KEY_L) switchFeed(-1);
	if (g_in.down & KEY_R) switchFeed(+1);
	if (tapIn(0, 0, 40, 24)) switchFeed(-1);
	if (tapIn(SCR_W - 40, 0, 40, 24)) switchFeed(+1);
	if (g_in.down & KEY_X) load();
	int old = s_lv.sel;
	int act = lvUpdate(&s_lv);
	if (old != s_lv.sel) sndMove();
	if (act >= 0 && act < s_count && s_items[act].link) browserOpenUrl(s_items[act].link);
	gfxInvalidate(GFX_BOT);
	if (jobBusy(&s_job) && (g_frame % 6) == 0) gfxInvalidate(GFX_TOP);
}

static void nDrawTop(Canvas* c) {
	cvClear(c, T.bg);
	uiStatusBar(c, false);
	uiHeader(c, IC_NEWS_20, FEEDS[s_feed].name);
	if (jobBusy(&s_job)) uiSpinner(c, SCR_W - 20, STATUS_H + 14, 7, T.accent);
	if (s_count && s_lv.sel < s_count) {
		const Item* it = &s_items[s_lv.sel];
		int y = STATUS_H + 34;
		y += cvTextWrap(c, FONT_TITLE, 10, y, SCR_W - 20, 3, T.text, it->title);
		char age[32];
		fmtAge(it->when, age, sizeof(age));
		char meta[128];
		snprintf(meta, sizeof(meta), "%s%s%s", s_feedTitle[0] ? s_feedTitle : FEEDS[s_feed].name, age[0] ? "  \xE2\x80\xA2  " : "", age);
		cvTextFit(c, FONT_SMALL, 10, y + 2, SCR_W - 20, T.accent, meta);
		y += 20;
		cvTextWrap(c, FONT_SMALL, 10, y, SCR_W - 20, (SCR_H - y - 4) / fontHeight(FONT_SMALL), T.text2, it->desc);
	} else if (!jobBusy(&s_job)) {
		uiEmpty(c, IC_NEWS_40, s_err[0] ? "N\xC3\xA3o foi poss\xC3\xADvel carregar" : "Sem not\xC3\xAD" "cias", s_err[0] ? s_err : NULL);
	}
}

static void nDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	// seletor de fonte
	cvIcon(c, IC_BACK_20, 8, 2, T.text2);
	cvIcon(c, IC_FWD_20, SCR_W - 28, 2, T.text2);
	char t[64];
	snprintf(t, sizeof(t), "%s  (%d/%d)", FEEDS[s_feed].name, s_feed + 1, (int)NFEEDS);
	cvTextC(c, FONT_TITLE, SCR_W / 2, 1, T.text, t);
	if (jobBusy(&s_job) && !s_count) {
		uiSpinner(c, SCR_W / 2, 96, 12, T.accent);
		cvTextC(c, FONT_BODY, SCR_W / 2, 118, T.text2, "Carregando\xE2\x80\xA6");
	} else {
		lvDraw(c, &s_lv);
	}
	static const Hint h[] = {{KEY_R, "Fonte"}, {KEY_X, "Atualizar"}, {KEY_A, "Ler"}, {KEY_B, "Voltar"}};
	uiHints(c, h, 4);
}

const App app_news = {"Not\xC3\xAD" "cias", nEnter, NULL, nFrame, nDrawTop, nDrawBot};

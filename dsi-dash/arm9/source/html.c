// DSi Dash — HTML simplificado: tokenizador, layout em linhas e desenho
#include "common.h"
#include "html.h"
#include <ctype.h>

// ---------------------------------------------------------------------------
// memoria do documento
// ---------------------------------------------------------------------------
#define GROW(arr, n, cap, type, min)                                        \
	do {                                                                    \
		if ((n) >= (cap)) {                                                 \
			int nc_ = (cap) ? (cap) * 2 : (min);                            \
			type* na_ = (type*)realloc((arr), nc_ * sizeof(type));          \
			if (!na_) return -1;                                            \
			(arr) = na_;                                                    \
			(cap) = nc_;                                                    \
		}                                                                   \
	} while (0)

static int poolAdd(Doc* d, const char* s, int n) {
	if (d->poolLen + n + 1 > d->poolCap) {
		int nc = d->poolCap ? d->poolCap * 2 : 16384;
		while (nc < d->poolLen + n + 1) nc *= 2;
		char* np = (char*)realloc(d->pool, nc);
		if (!np) return -1;
		d->pool = np;
		d->poolCap = nc;
	}
	int off = d->poolLen;
	memcpy(d->pool + off, s, n);
	d->poolLen += n;
	d->pool[d->poolLen] = 0;
	return off;
}

static int poolStr(Doc* d, const char* s) {
	int off = poolAdd(d, s, strlen(s));
	if (off >= 0) d->poolLen++;  // mantem o NUL
	return off;
}

void docFree(Doc* d) {
	if (!d) return;
	for (int i = 0; i < d->nFields; i++) free(d->fields[i].value);
	free(d->pool);
	free(d->runs);
	free(d->links);
	free(d->fields);
	free(d->forms);
	free(d->imgs);
	free(d->focus);
	free(d);
}

// ---------------------------------------------------------------------------
// entidades / charset
// ---------------------------------------------------------------------------
typedef struct Ent {
	const char* n;
	u16 cp;
} Ent;

static const Ent ENTS[] = {
	{"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
	{"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"hellip", 0x2026}, {"mdash", 0x2014},
	{"ndash", 0x2013}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
	{"sbquo", 0x201A}, {"bdquo", 0x201E}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"bull", 0x2022},
	{"middot", 0xB7}, {"deg", 0xB0}, {"euro", 0x20AC}, {"pound", 0xA3}, {"yen", 0xA5},
	{"cent", 0xA2}, {"sect", 0xA7}, {"para", 0xB6}, {"plusmn", 0xB1}, {"frac12", 0xBD},
	{"frac14", 0xBC}, {"frac34", 0xBE}, {"sup1", 0xB9}, {"sup2", 0xB2}, {"sup3", 0xB3},
	{"iexcl", 0xA1}, {"iquest", 0xBF}, {"ordf", 0xAA}, {"ordm", 0xBA}, {"acute", 0xB4},
	{"cedil", 0xB8}, {"uml", 0xA8}, {"macr", 0xAF}, {"shy", 0xAD}, {"micro", 0xB5},
	{"not", 0xAC}, {"brvbar", 0xA6}, {"curren", 0xA4}, {"larr", 0x2190}, {"uarr", 0x2191},
	{"rarr", 0x2192}, {"darr", 0x2193}, {"le", 0x2264}, {"ge", 0x2265}, {"ne", '!'},
	{"minus", '-'}, {"ensp", ' '}, {"emsp", ' '}, {"thinsp", ' '}, {"zwj", 0x200D}, {"zwnj", 0x200C},
	{"lrm", 0x200E}, {"rlm", 0x200F}, {"prime", '\''}, {"Prime", '"'}, {"times", 0xD7}, {"divide", 0xF7},
};

static const char* LAT1[] = {
	"Agrave", "Aacute", "Acirc", "Atilde", "Auml", "Aring", "AElig", "Ccedil", "Egrave", "Eacute", "Ecirc", "Euml",
	"Igrave", "Iacute", "Icirc", "Iuml", "ETH", "Ntilde", "Ograve", "Oacute", "Ocirc", "Otilde", "Ouml", "times",
	"Oslash", "Ugrave", "Uacute", "Ucirc", "Uuml", "Yacute", "THORN", "szlig", "agrave", "aacute", "acirc", "atilde",
	"auml", "aring", "aelig", "ccedil", "egrave", "eacute", "ecirc", "euml", "igrave", "iacute", "icirc", "iuml",
	"eth", "ntilde", "ograve", "oacute", "ocirc", "otilde", "ouml", "divide", "oslash", "ugrave", "uacute", "ucirc",
	"uuml", "yacute", "thorn", "yuml",
};

static const u16 CP1252[32] = {
	0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
	0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178,
};

static int putUtf8(char* o, u32 c) {
	if (c < 0x80) {
		o[0] = c;
		return 1;
	}
	if (c < 0x800) {
		o[0] = 0xC0 | (c >> 6);
		o[1] = 0x80 | (c & 0x3F);
		return 2;
	}
	if (c < 0x10000) {
		o[0] = 0xE0 | (c >> 12);
		o[1] = 0x80 | ((c >> 6) & 0x3F);
		o[2] = 0x80 | (c & 0x3F);
		return 3;
	}
	o[0] = 0xF0 | (c >> 18);
	o[1] = 0x80 | ((c >> 12) & 0x3F);
	o[2] = 0x80 | ((c >> 6) & 0x3F);
	o[3] = 0x80 | (c & 0x3F);
	return 4;
}

// decodifica "&...;" em s; retorna bytes consumidos (0 se nao for entidade) e escreve UTF-8 em out
static int decodeEntity(const char* s, char* out, int* outLen) {
	if (s[0] != '&') return 0;
	u32 cp = 0;
	int i = 1;
	if (s[1] == '#') {
		i = 2;
		bool hex = (s[2] == 'x' || s[2] == 'X');
		if (hex) i++;
		int st = i;
		while (hex ? isxdigit((u8)s[i]) : isdigit((u8)s[i])) {
			cp = cp * (hex ? 16 : 10) + (isdigit((u8)s[i]) ? s[i] - '0' : (tolower((u8)s[i]) - 'a' + 10));
			if (cp > 0x10FFFF) cp = '?';
			i++;
		}
		if (i == st) return 0;
		if (s[i] == ';') i++;
		if (cp >= 0x80 && cp < 0xA0) cp = CP1252[cp - 0x80];
		if (cp == 0) cp = '?';
	} else {
		char name[12];
		int n = 0;
		while (n < 10 && isalnum((u8)s[i])) name[n++] = s[i++];
		name[n] = 0;
		if (!n) return 0;
		bool found = false;
		for (int k = 0; k < ARRAY_SIZE(ENTS); k++)
			if (!strcmp(ENTS[k].n, name)) {
				cp = ENTS[k].cp;
				found = true;
				break;
			}
		if (!found)
			for (int k = 0; k < ARRAY_SIZE(LAT1); k++)
				if (!strcmp(LAT1[k], name)) {
					cp = 0xC0 + k;
					found = true;
					break;
				}
		if (!found) return 0;
		if (s[i] == ';') i++;
	}
	*outLen = putUtf8(out, cp);
	return i;
}

static char* toUtf8(const char* in, int len, int* outLen) {
	char* o = (char*)malloc(len * 3 + 1);
	if (!o) return NULL;
	int n = 0;
	for (int i = 0; i < len; i++) {
		u8 c = (u8)in[i];
		if (c < 0x80) o[n++] = c;
		else if (c < 0xA0) n += putUtf8(o + n, CP1252[c - 0x80]);
		else n += putUtf8(o + n, c);
	}
	o[n] = 0;
	*outLen = n;
	return o;
}

static bool isLatinCharset(const char* s) {
	if (!s) return false;
	char b[48];
	int n = 0;
	for (; *s && n < 47; s++) b[n++] = tolower((u8)*s);
	b[n] = 0;
	return strstr(b, "8859-1") || strstr(b, "latin1") || strstr(b, "latin-1") || strstr(b, "1252") || strstr(b, "8859-15");
}

static const char* findCharset(const char* html, int len, const char* ctype) {
	const char* c = ctype ? strcasestr(ctype, "charset=") : NULL;
	if (c) return c + 8;
	int lim = MIN(len, 4096);
	static char tmp[4097];
	memcpy(tmp, html, lim);
	tmp[lim] = 0;
	c = strcasestr(tmp, "charset=");
	if (c) {
		c += 8;
		while (*c == '"' || *c == '\'') c++;
		return c;
	}
	return NULL;
}

// ---------------------------------------------------------------------------
// layout
// ---------------------------------------------------------------------------
typedef struct Lay {
	Doc* d;
	int W;
	int x, y;
	int indent;
	int lineRun, lineField, lineImg, lineFocus;
	bool lineEmpty;
	bool space;           // espaco pendente
	int margin;           // margem vertical pendente
	int bold, heading, code, muted, pre, under;
	int link;
	int skip;
	char skipTag[12];
	int listDepth;
	int olNum[8];
	bool olType[8];
	int form;
	bool inTitle;
	int titleLen;
	int capture;          // capturando texto p/ campo (button/textarea/option)
	int captureField;
	char base[1024];
	int cellInRow;
	DocImgLookup lookup;
	int focusedLink;
} Lay;

static int curFont(Lay* L) {
	if (L->heading == 1) return FONT_H1;
	if (L->heading == 2) return FONT_H2;
	if (L->heading >= 3) return FONT_TEXTB;
	return L->bold ? FONT_TEXTB : FONT_TEXT;
}

static int curColor(Lay* L) {
	if (L->link >= 0) return DC_LINK;
	if (L->heading) return DC_HEAD;
	if (L->code) return DC_CODE;
	if (L->muted) return DC_MUTED;
	return DC_TEXT;
}

static int addFocus(Lay* L, int kind, int idx) {
	Doc* d = L->d;
	GROW(d->focus, d->nFocus, d->capFocus, DocFocus, 64);
	d->focus[d->nFocus++] = (DocFocus){kind, idx, 0, 0};
	return 0;
}

static void flushLine(Lay* L) {
	Doc* d = L->d;
	if (L->lineEmpty) {
		L->x = L->indent;
		L->space = false;
		return;
	}
	int asc = 0, desc = 0;
	for (int i = L->lineRun; i < d->nRuns; i++) {
		int a = fontAscent(d->runs[i].font), h = fontHeight(d->runs[i].font);
		if (d->runs[i].flags & RF_RULE) {
			a = 4;
			h = 8;
		}
		asc = MAX(asc, a);
		desc = MAX(desc, h - a);
	}
	for (int i = L->lineField; i < d->nFields; i++) {
		if (d->fields[i].type == FT_HIDDEN) continue;
		asc = MAX(asc, d->fields[i].h - 5);
		desc = MAX(desc, 5);
	}
	for (int i = L->lineImg; i < d->nImgs; i++) {
		if (!d->imgs[i].placed) continue;
		asc = MAX(asc, d->imgs[i].h - 3);
		desc = MAX(desc, 3);
	}
	int y = L->y;
	for (int i = L->lineRun; i < d->nRuns; i++) {
		DocRun* r = &d->runs[i];
		if (r->flags & RF_RULE) {
			r->y = y + asc - 1;
			continue;
		}
		r->y = y + asc - fontAscent(r->font);
		r->h = fontHeight(r->font);
	}
	for (int i = L->lineField; i < d->nFields; i++) d->fields[i].y = y + asc - (d->fields[i].h - 5);
	for (int i = L->lineImg; i < d->nImgs; i++)
		if (d->imgs[i].placed) d->imgs[i].y = y + asc - (d->imgs[i].h - 3);
	for (int i = L->lineFocus; i < d->nFocus; i++) {
		DocFocus* f = &d->focus[i];
		if (f->kind == FK_LINK) {
			// link so com imagem: posicao da imagem
			for (int k = L->lineImg; k < d->nImgs; k++)
				if (d->imgs[k].placed && d->imgs[k].link == f->idx) {
					f->y = d->imgs[k].y;
					f->x = d->imgs[k].x;
				}
		}
	}
	for (int i = L->lineFocus; i < d->nFocus; i++) {
		DocFocus* f = &d->focus[i];
		if (f->kind == FK_FIELD) {
			f->y = d->fields[f->idx].y;
			f->x = d->fields[f->idx].x;
		}
	}
	L->y = y + asc + desc + 1;
	L->lineRun = d->nRuns;
	L->lineField = d->nFields;
	L->lineImg = d->nImgs;
	L->lineFocus = d->nFocus;
	L->x = L->indent;
	L->lineEmpty = true;
	L->space = false;
}

static void block(Lay* L, int m) {
	flushLine(L);
	L->margin = MAX(L->margin, m);
}

static void beginContent(Lay* L) {
	if (L->lineEmpty && L->margin) {
		if (L->d->nRuns || L->d->nFields) L->y += L->margin;
		L->margin = 0;
	}
}

static int addRun(Lay* L, int x, int w, const char* s, int n, int flags) {
	Doc* d = L->d;
	GROW(d->runs, d->nRuns, d->capRuns, DocRun, 256);
	int off = poolAdd(d, s, n);
	if (off < 0) return -1;
	DocRun* r = &d->runs[d->nRuns++];
	r->y = 0;
	r->x = x;
	r->w = w;
	r->font = curFont(L);
	r->color = curColor(L);
	r->flags = flags | ((L->link >= 0 || L->under) ? RF_UNDER : 0);
	r->h = fontHeight(r->font);
	r->link = L->link;
	r->off = off;
	r->len = n;
	return 0;
}

// um "atomo" de texto (palavra) ja medido
static void layWord(Lay* L, const char* w, int n) {
	Doc* d = L->d;
	int f = curFont(L);
	int ww = textWidthN(f, w, n);
	int sp = (L->space && !L->lineEmpty) ? textWidth(f, " ") : 0;
	if (!L->lineEmpty && L->x + sp + ww > L->W) {
		flushLine(L);
		sp = 0;
	}
	beginContent(L);
	// palavra maior que a linha: quebra por caracteres
	while (ww > L->W - L->indent && n > 1) {
		int fit = textFitBytes(f, w, L->W - L->x);
		if (fit <= 0) {
			flushLine(L);
			beginContent(L);
			fit = textFitBytes(f, w, L->W - L->x);
			if (fit <= 0) fit = 1;
		}
		addRun(L, L->x, textWidthN(f, w, fit), w, fit, 0);
		L->lineEmpty = false;
		w += fit;
		n -= fit;
		flushLine(L);
		beginContent(L);
		ww = textWidthN(f, w, n);
		sp = 0;
	}
	// junta com o run anterior se tiver o mesmo estilo e estiver no fim do pool
	if (d->nRuns > L->lineRun) {
		DocRun* r = &d->runs[d->nRuns - 1];
		if (r->font == f && r->color == curColor(L) && r->link == L->link && !(r->flags & (RF_RULE | RF_IMGBOX)) &&
			(int)(r->off + r->len) == d->poolLen && r->x + r->w == L->x && ((r->flags & RF_UNDER) != 0) == (L->link >= 0 || L->under) &&
			r->len + n + 1 < 60000) {
			if (sp) poolAdd(d, " ", 1), r->len++;
			poolAdd(d, w, n);
			r->len += n;
			r->w += sp + ww;
			L->x += sp + ww;
			L->space = false;
			return;
		}
	}
	L->x += sp;
	if (addRun(L, L->x, ww, w, n, 0) < 0) return;
	if (L->link >= 0 && L->focusedLink != L->link) {
		addFocus(L, FK_LINK, L->link);
		L->focusedLink = L->link;
	}
	L->x += ww;
	L->lineEmpty = false;
	L->space = false;
}

static void layText(Lay* L, const char* s, int n) {
	if (L->skip) return;
	if (L->inTitle) {
		Doc* d = L->d;
		for (int i = 0; i < n && L->titleLen < (int)sizeof(d->title) - 1; i++) {
			char ch = s[i];
			if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
			if (ch == ' ' && (L->titleLen == 0 || d->title[L->titleLen - 1] == ' ')) continue;
			d->title[L->titleLen++] = ch;
		}
		d->title[L->titleLen] = 0;
		return;
	}
	if (L->capture) {
		DocField* f = &L->d->fields[L->captureField];
		int cur = f->value ? strlen(f->value) : 0;
		char* nv = (char*)realloc(f->value, cur + n + 1);
		if (!nv) return;
		memcpy(nv + cur, s, n);
		nv[cur + n] = 0;
		f->value = nv;
		return;
	}
	const char* e = s + n;
	if (L->pre) {
		const char* p = s;
		while (p < e) {
			const char* q = p;
			while (q < e && *q != '\n') q++;
			if (q > p) {
				// preserva espacos: cada linha vira palavras separadas por espacos visiveis
				const char* a = p;
				while (a < q) {
					const char* b = a;
					while (b < q && *b != ' ' && *b != '\t') b++;
					if (b > a) layWord(L, a, b - a);
					int sp = 0;
					while (b < q && (*b == ' ' || *b == '\t')) sp += (*b == '\t') ? 4 : 1, b++;
					if (sp) {
						beginContent(L);
						L->x += sp * textWidth(curFont(L), " ");
						L->lineEmpty = false;
					}
					a = b;
				}
			}
			if (q < e) {
				if (L->lineEmpty) L->y += fontHeight(FONT_TEXT);
				flushLine(L);
			}
			p = q + 1;
		}
		return;
	}
	const char* p = s;
	while (p < e) {
		if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
			L->space = true;
			p++;
			continue;
		}
		const char* q = p;
		while (q < e && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t') q++;
		layWord(L, p, q - p);
		p = q;
	}
}

// caixa atomica (campo ou imagem)
static void layBox(Lay* L, int w, int h, int* ox) {
	if (!L->lineEmpty && L->x + w + 4 > L->W) flushLine(L);
	beginContent(L);
	if (!L->lineEmpty) L->x += 4;
	*ox = L->x;
	L->x += w;
	L->lineEmpty = false;
	L->space = false;
	(void)h;
}

// ---------------------------------------------------------------------------
// atributos
// ---------------------------------------------------------------------------
typedef struct Attr {
	char name[16];
	char* val;
} Attr;

typedef struct Tag {
	char name[16];
	bool close, selfClose;
	Attr a[16];
	int na;
	char buf[2048];
	int bufLen;
} Tag;

static const char* attr(const Tag* t, const char* name) {
	for (int i = 0; i < t->na; i++)
		if (!strcmp(t->a[i].name, name)) return t->a[i].val;
	return NULL;
}

// decodifica entidades num valor de atributo
static int decodeInto(const char* s, int n, char* out, int sz) {
	int o = 0;
	for (int i = 0; i < n && o < sz - 5;) {
		if (s[i] == '&') {
			char tmp[8];
			int tl = 0;
			int k = decodeEntity(s + i, tmp, &tl);
			if (k) {
				memcpy(out + o, tmp, tl);
				o += tl;
				i += k;
				continue;
			}
		}
		char ch = s[i++];
		if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
		out[o++] = ch;
	}
	out[o] = 0;
	return o;
}

// le uma tag a partir de p (apos '<'); retorna ponteiro apos '>'
static const char* parseTag(const char* p, const char* end, Tag* t) {
	t->na = 0;
	t->bufLen = 0;
	t->close = t->selfClose = false;
	if (*p == '/') {
		t->close = true;
		p++;
	}
	int n = 0;
	while (p < end && (isalnum((u8)*p) || *p == '-' || *p == ':') && n < 15) t->name[n++] = tolower((u8)*p++);
	t->name[n] = 0;
	while (p < end && (isalnum((u8)*p) || *p == '-' || *p == ':')) p++;
	for (;;) {
		while (p < end && isspace((u8)*p)) p++;
		if (p >= end) break;
		if (*p == '>') {
			p++;
			break;
		}
		if (*p == '/') {
			t->selfClose = true;
			p++;
			continue;
		}
		char an[16];
		int al = 0;
		while (p < end && !isspace((u8)*p) && *p != '=' && *p != '>' && *p != '/') {
			if (al < 15) an[al++] = tolower((u8)*p);
			p++;
		}
		an[al] = 0;
		if (!al) {
			p++;
			continue;
		}
		while (p < end && isspace((u8)*p)) p++;
		const char* vs = "";
		int vl = 0;
		if (p < end && *p == '=') {
			p++;
			while (p < end && isspace((u8)*p)) p++;
			if (p < end && (*p == '"' || *p == '\'')) {
				char q = *p++;
				vs = p;
				while (p < end && *p != q) p++;
				vl = p - vs;
				if (p < end) p++;
			} else {
				vs = p;
				while (p < end && !isspace((u8)*p) && *p != '>') p++;
				vl = p - vs;
			}
		}
		if (t->na < 16 && t->bufLen < (int)sizeof(t->buf) - 8) {
			Attr* a = &t->a[t->na++];
			snprintf(a->name, sizeof(a->name), "%s", an);
			a->val = t->buf + t->bufLen;
			int w = decodeInto(vs, vl, a->val, sizeof(t->buf) - t->bufLen - 1);
			t->bufLen += w + 1;
		}
	}
	return p;
}

static bool tagIs(const Tag* t, const char* list) {
	// list: "p div section" (separado por espaco)
	int n = strlen(t->name);
	const char* s = list;
	while (*s) {
		const char* e = strchr(s, ' ');
		int l = e ? (int)(e - s) : (int)strlen(s);
		if (l == n && !strncmp(s, t->name, n)) return true;
		if (!e) break;
		s = e + 1;
	}
	return false;
}

static int addLink(Lay* L, const char* href) {
	Doc* d = L->d;
	char* abs = (char*)malloc(1024);
	if (!abs) return -1;
	urlResolve(L->base, href, abs, 1024);
	GROW(d->links, d->nLinks, d->capLinks, u32, 64);
	int off = poolStr(d, abs);
	free(abs);
	if (off < 0) return -1;
	d->links[d->nLinks] = off;
	return d->nLinks++;
}

static int addField(Lay* L, int type, const char* name, const char* value, int w, int h) {
	Doc* d = L->d;
	GROW(d->fields, d->nFields, d->capFields, DocField, 16);
	DocField* f = &d->fields[d->nFields];
	memset(f, 0, sizeof(*f));
	f->type = type;
	f->form = L->form;
	f->nameOff = poolStr(d, name ? name : "");
	f->value = strdup(value ? value : "");
	f->w = w;
	f->h = h;
	if (type != FT_HIDDEN) {
		int x;
		layBox(L, w, h, &x);
		f->x = x;
	}
	int idx = d->nFields++;
	if (type != FT_HIDDEN) addFocus(L, FK_FIELD, idx);
	return idx;
}

static void listItemMarker(Lay* L) {
	flushLine(L);
	beginContent(L);
	char mk[16];
	int lv = CLAMP(L->listDepth - 1, 0, 7);
	if (L->listDepth > 0 && L->olType[lv]) snprintf(mk, sizeof(mk), "%d.", ++L->olNum[lv]);
	else snprintf(mk, sizeof(mk), "\xE2\x80\xA2");
	int f = curFont(L);
	int w = textWidth(f, mk);
	int saveLink = L->link;
	L->link = -1;
	addRun(L, L->indent - w - 4, w, mk, strlen(mk), 0);
	L->link = saveLink;
	L->d->runs[L->d->nRuns - 1].color = DC_MUTED;
	L->lineEmpty = false;
	L->x = L->indent;
}

static void handleTag(Lay* L, Tag* t) {
	Doc* d = L->d;
	const char* n = t->name;
	if (L->skip) {
		if (!strcmp(n, L->skipTag)) L->skip += t->close ? -1 : (t->selfClose ? 0 : 1);
		return;
	}
	if (!t->close && tagIs(t, "script style svg template iframe object noembed math head-hidden select")) {
		if (!t->selfClose) {
			snprintf(L->skipTag, sizeof(L->skipTag), "%s", n);
			L->skip = 1;
		}
		return;
	}
	if (!strcmp(n, "title")) {
		L->inTitle = !t->close && !d->title[0];
		return;
	}
	if (!strcmp(n, "base") && !t->close) {
		const char* h = attr(t, "href");
		if (h) {
			char tmp[1024];
			urlResolve(L->base, h, tmp, sizeof(tmp));
			snprintf(L->base, sizeof(L->base), "%s", tmp);
		}
		return;
	}
	if (L->capture) {
		if (t->close && (!strcmp(n, "button") || !strcmp(n, "textarea"))) L->capture = 0;
		return;
	}
	// blocos
	if (tagIs(t, "p")) return block(L, 7);
	if (tagIs(t, "div section article header footer nav aside main figure address center form fieldset details summary dl caption body html")) {
		if (!strcmp(n, "form")) {
			if (!t->close) {
				if (d->nForms >= d->capForms) {
					int nc = d->capForms ? d->capForms * 2 : 4;
					DocForm* nf = (DocForm*)realloc(d->forms, nc * sizeof(DocForm));
					if (!nf) return;
					d->forms = nf;
					d->capForms = nc;
				}
				const char* act = attr(t, "action");
				const char* m = attr(t, "method");
				char* abs = (char*)malloc(1024);
				if (!abs) return;
				urlResolve(L->base, act && act[0] ? act : d->url, abs, 1024);
				d->forms[d->nForms].actionOff = poolStr(d, abs);
				d->forms[d->nForms].post = m && !strcasecmp(m, "post");
				free(abs);
				L->form = d->nForms++;
			} else {
				L->form = -1;
			}
		}
		return block(L, 0);
	}
	if (tagIs(t, "h1 h2 h3 h4 h5 h6")) {
		int lv = n[1] - '0';
		block(L, lv <= 2 ? 11 : 8);
		L->heading = t->close ? 0 : lv;
		if (t->close) L->margin = MAX(L->margin, 5);
		return;
	}
	if (tagIs(t, "ul ol menu")) {
		block(L, L->listDepth ? 2 : 6);
		if (!t->close) {
			if (L->listDepth < 8) {
				L->olType[L->listDepth] = !strcmp(n, "ol");
				L->olNum[L->listDepth] = 0;
				const char* st = attr(t, "start");
				if (st) L->olNum[L->listDepth] = atoi(st) - 1;
			}
			L->listDepth++;
			L->indent = MIN(L->indent + 16, L->W / 2);
		} else if (L->listDepth > 0) {
			L->listDepth--;
			L->indent = MAX(0, L->indent - 16);
		}
		L->x = L->indent;
		return;
	}
	if (!strcmp(n, "li")) {
		if (!t->close) {
			if (L->listDepth == 0) {
				L->listDepth = 1;
				L->olType[0] = false;
				L->indent = 16;
			}
			L->margin = MAX(L->margin, 3);
			listItemMarker(L);
		} else {
			flushLine(L);
		}
		return;
	}
	if (tagIs(t, "dt")) return block(L, 4);
	if (tagIs(t, "dd")) {
		block(L, 2);
		L->indent = t->close ? MAX(0, L->indent - 12) : L->indent + 12;
		L->x = L->indent;
		return;
	}
	if (!strcmp(n, "blockquote")) {
		block(L, 7);
		L->indent = t->close ? MAX(0, L->indent - 12) : L->indent + 12;
		L->muted += t->close ? -1 : 1;
		L->muted = MAX(0, L->muted);
		L->x = L->indent;
		return;
	}
	if (!strcmp(n, "pre")) {
		block(L, 7);
		L->pre = t->close ? 0 : 1;
		L->code = t->close ? MAX(0, L->code - 1) : L->code + 1;
		return;
	}
	if (tagIs(t, "table")) {
		block(L, 6);
		return;
	}
	if (!strcmp(n, "tr")) {
		block(L, 2);
		L->cellInRow = 0;
		return;
	}
	if (tagIs(t, "td th")) {
		if (!t->close) {
			if (L->cellInRow++ > 0) {
				L->space = true;
				layWord(L, "\xC2\xB7", 2);
				L->space = true;
			}
			if (!strcmp(n, "th")) L->bold++;
		} else if (!strcmp(n, "th")) {
			L->bold = MAX(0, L->bold - 1);
		}
		return;
	}
	if (!strcmp(n, "br")) {
		if (L->lineEmpty) {
			beginContent(L);
			L->y += fontHeight(FONT_TEXT);
		}
		flushLine(L);
		return;
	}
	if (!strcmp(n, "hr")) {
		block(L, 6);
		beginContent(L);
		addRun(L, 0, L->W, "", 0, RF_RULE);
		L->lineEmpty = false;
		flushLine(L);
		L->margin = 6;
		return;
	}
	// inline
	if (tagIs(t, "b strong th dt")) {
		L->bold += t->close ? -1 : 1;
		L->bold = MAX(0, L->bold);
		return;
	}
	if (tagIs(t, "code kbd samp tt var")) {
		L->code += t->close ? -1 : 1;
		L->code = MAX(0, L->code);
		return;
	}
	if (tagIs(t, "small figcaption cite")) {
		L->muted += t->close ? -1 : 1;
		L->muted = MAX(0, L->muted);
		if (!strcmp(n, "figcaption")) block(L, 2);
		return;
	}
	if (tagIs(t, "u ins")) {
		L->under += t->close ? -1 : 1;
		L->under = MAX(0, L->under);
		return;
	}
	if (!strcmp(n, "a")) {
		if (t->close) {
			L->link = -1;
			return;
		}
		const char* h = attr(t, "href");
		L->link = (h && h[0] && strncasecmp(h, "javascript:", 11)) ? addLink(L, h) : -1;
		return;
	}
	if (!strcmp(n, "img") && !t->close) {
		const char* alt = attr(t, "alt");
		const char* w = attr(t, "width");
		const char* h = attr(t, "height");
		if ((w && atoi(w) > 0 && atoi(w) < 32) || (h && atoi(h) > 0 && atoi(h) < 32)) return;  // icones/rastreadores
		const char* src = attr(t, "src");
		const char* lazy = attr(t, "data-src");
		if (!lazy) lazy = attr(t, "data-original");
		if (lazy && lazy[0] && (!src || !src[0] || !strncasecmp(src, "data:", 5))) src = lazy;
		bool usable = src && src[0] && strncasecmp(src, "data:", 5);
		if (usable) {  // .svg puro nao (mas .svg.png, miniatura rasterizada, sim)
			const char* q = strchr(src, '?');
			int sl = q ? (int)(q - src) : (int)strlen(src);
			if (sl >= 4 && !strncasecmp(src + sl - 4, ".svg", 4)) usable = false;
		}
		if (usable && d->nImgs < 64) {
			char* abs = (char*)malloc(1024);
			if (abs) {
				urlResolve(L->base, src, abs, 1024);
				if (d->nImgs >= d->capImgs) {
					int nc = d->capImgs ? d->capImgs * 2 : 8;
					DocImg* ni = (DocImg*)realloc(d->imgs, nc * sizeof(DocImg));
					if (ni) {
						d->imgs = ni;
						d->capImgs = nc;
					}
				}
				if (d->nImgs < d->capImgs) {
					DocImg* im = &d->imgs[d->nImgs];
					memset(im, 0, sizeof(*im));
					im->srcOff = poolStr(d, abs);
					im->link = L->link;
					u16* px;
					int iw, ih;
					if (L->lookup && L->lookup(abs, &px, &iw, &ih)) {
						bool big = iw > L->W / 2;
						if (big) block(L, 4);
						int x;
						layBox(L, iw, ih, &x);
						im->px = px;
						im->x = x;
						im->w = iw;
						im->h = ih;
						im->placed = true;
						d->nImgs++;
						if (L->link >= 0 && L->focusedLink != L->link) {
							addFocus(L, FK_LINK, L->link);
							L->focusedLink = L->link;
						}
						if (big) {
							flushLine(L);
							L->margin = MAX(L->margin, 4);
						}
						free(abs);
						return;
					}
					d->nImgs++;
				}
				free(abs);
			}
		}
		if (alt && alt[0]) {
			L->muted++;
			L->space = true;
			char tmp[160];
			snprintf(tmp, sizeof(tmp), "[%s]", alt);
			layText(L, tmp, strlen(tmp));
			L->space = true;
			L->muted--;
		}
		return;
	}
	if (!strcmp(n, "input") && !t->close) {
		const char* ty = attr(t, "type");
		const char* nm = attr(t, "name");
		const char* val = attr(t, "value");
		if (!ty) ty = "text";
		if (!strcasecmp(ty, "hidden")) {
			addField(L, FT_HIDDEN, nm, val, 0, 0);
		} else if (!strcasecmp(ty, "submit") || !strcasecmp(ty, "button") || !strcasecmp(ty, "image")) {
			const char* lab = val && val[0] ? val : "Enviar";
			if (!strcasecmp(ty, "image")) {
				const char* alt = attr(t, "alt");
				lab = alt && alt[0] ? alt : "Enviar";
			}
			addField(L, FT_SUBMIT, nm, lab, MIN(L->W, textWidth(FONT_TEXT, lab) + 18), 20);
		} else if (!strcasecmp(ty, "checkbox") || !strcasecmp(ty, "radio")) {
			int i = addField(L, !strcasecmp(ty, "radio") ? FT_RADIO : FT_CHECKBOX, nm, val ? val : "on", 14, 14);
			if (i >= 0 && attr(t, "checked")) d->fields[i].checked = 1;
		} else if (strcasecmp(ty, "file") && strcasecmp(ty, "reset")) {
			const char* sz = attr(t, "size");
			int w = sz ? CLAMP(atoi(sz) * 6, 60, L->W) : MIN(L->W, 160);
			const char* ph = attr(t, "placeholder");
			int i = addField(L, !strcasecmp(ty, "password") ? FT_PASSWORD : FT_TEXT, nm, val, w, 22);
			(void)ph;
			(void)i;
		}
		return;
	}
	if (!strcmp(n, "button") && !t->close) {
		const char* ty = attr(t, "type");
		if (ty && strcasecmp(ty, "submit")) {
			L->capture = 0;
			return;
		}
		int i = addField(L, FT_SUBMIT, attr(t, "name"), "", 60, 20);
		if (i >= 0) {
			L->capture = 1;
			L->captureField = i;
		}
		return;
	}
	if (!strcmp(n, "textarea") && !t->close) {
		int i = addField(L, FT_TEXTAREA, attr(t, "name"), "", L->W - L->x, 44);
		if (i >= 0) {
			L->capture = 1;
			L->captureField = i;
		}
		return;
	}
}

// ajusta a largura de botoes capturados
static void fixButtons(Doc* d) {
	for (int i = 0; i < d->nFields; i++) {
		DocField* f = &d->fields[i];
		if (f->type == FT_SUBMIT) {
			if (!f->value[0]) {
				free(f->value);
				f->value = strdup("Enviar");
			}
			// normaliza espacos
			char* s = f->value;
			int o = 0;
			bool sp = false;
			for (int k = 0; s[k]; k++) {
				char ch = s[k];
				if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
				if (ch == ' ' && (sp || o == 0)) continue;
				sp = ch == ' ';
				s[o++] = ch;
			}
			while (o > 0 && s[o - 1] == ' ') o--;
			s[o] = 0;
		}
	}
}

static Doc* newDoc(const char* url) {
	Doc* d = (Doc*)calloc(1, sizeof(Doc));
	if (!d) return NULL;
	snprintf(d->url, sizeof(d->url), "%s", url ? url : "");
	return d;
}

Doc* docParseHtml(const char* html0, int len, const char* url, const char* ctype) { return docParseHtmlEx(html0, len, url, ctype, NULL); }

Doc* docParseHtmlEx(const char* html0, int len, const char* url, const char* ctype, DocImgLookup lookup) {
	Doc* d = newDoc(url);
	if (!d) return NULL;
	char* conv = NULL;
	const char* html = html0;
	if (isLatinCharset(findCharset(html0, len, ctype))) {
		int nl;
		conv = toUtf8(html0, len, &nl);
		if (conv) {
			html = conv;
			len = nl;
		}
	}
	Lay* L = (Lay*)calloc(1, sizeof(Lay));
	Tag* t = (Tag*)malloc(sizeof(Tag));
	char* tb = (char*)malloc(4096);
	if (!L || !t || !tb) {
		free(L);
		free(t);
		free(tb);
		free(conv);
		docFree(d);
		return NULL;
	}
	L->d = d;
	L->W = SCR_W - 2 * DOC_MARGIN;
	L->lineEmpty = true;
	L->link = -1;
	L->form = -1;
	L->focusedLink = -1;
	L->lookup = lookup;
	snprintf(L->base, sizeof(L->base), "%s", url ? url : "");

	const char* p = html;
	const char* end = html + len;
	int maxRuns = sysIsDSi() ? 60000 : 12000;
	while (p < end && d->nRuns < maxRuns) {
		if (*p == '<') {
			if (!strncmp(p, "<!--", 4)) {
				const char* e = strstr(p + 4, "-->");
				p = e ? e + 3 : end;
				continue;
			}
			if (p[1] == '!' || p[1] == '?') {
				const char* e = memchr(p, '>', end - p);
				p = e ? e + 1 : end;
				continue;
			}
			if (!(isalpha((u8)p[1]) || (p[1] == '/' && isalpha((u8)p[2])))) {
				layText(L, p, 1);
				p++;
				continue;
			}
			p = parseTag(p + 1, end, t);
			handleTag(L, t);
			// conteudo "cru" (script/style/textarea)
			if (!t->close && !t->selfClose && tagIs(t, "script style textarea")) {
				char closeTag[20];
				snprintf(closeTag, sizeof(closeTag), "</%s", t->name);
				const char* e = strcasestr(p, closeTag);
				if (!e) e = end;
				if (!strcmp(t->name, "textarea") && L->capture) {
					int k = decodeInto(p, e - p, tb, 4096);
					layText(L, tb, k);
					L->capture = 0;
				}
				if (L->skip && !strcmp(L->skipTag, t->name)) L->skip = 0;
				p = e;
				const char* gt = memchr(p, '>', end - p);
				p = gt ? gt + 1 : end;
			}
			continue;
		}
		// texto ate a proxima tag, decodificando entidades
		const char* q = p;
		while (q < end && *q != '<') q++;
		while (p < q) {
			int o = 0;
			while (p < q && o < 4000) {
				if (*p == '&') {
					char tmp[8];
					int tl = 0;
					int k = decodeEntity(p, tmp, &tl);
					if (k) {
						if (tl == 2 && (u8)tmp[0] == 0xC2 && (u8)tmp[1] == 0xA0) {  // nbsp -> espaco
							tb[o++] = ' ';
						} else {
							memcpy(tb + o, tmp, tl);
							o += tl;
						}
						p += k;
						continue;
					}
				}
				tb[o++] = *p++;
			}
			layText(L, tb, o);
		}
	}
	flushLine(L);
	d->height = L->y + 16;
	fixButtons(d);
	if (!d->title[0] && url) snprintf(d->title, sizeof(d->title), "%s", url);
	free(L);
	free(t);
	free(tb);
	free(conv);
	return d;
}

Doc* docFromText(const char* txt, int len, const char* url) {
	Doc* d = newDoc(url);
	if (!d) return NULL;
	Lay* L = (Lay*)calloc(1, sizeof(Lay));
	if (!L) {
		docFree(d);
		return NULL;
	}
	L->d = d;
	L->W = SCR_W - 2 * DOC_MARGIN;
	L->lineEmpty = true;
	L->link = -1;
	L->form = -1;
	L->pre = 1;
	layText(L, txt, len);
	flushLine(L);
	d->height = L->y + 16;
	snprintf(d->title, sizeof(d->title), "%s", url ? url : "Texto");
	free(L);
	return d;
}

// ---------------------------------------------------------------------------
// desenho / interacao
// ---------------------------------------------------------------------------
static u16 docColor(int c) {
	switch (c) {
		case DC_LINK: return T.dark ? HEX(0x7DB5FF) : HEX(0x1A64C8);
		case DC_HEAD: return T.text;
		case DC_MUTED: return T.text2;
		case DC_CODE: return T.dark ? HEX(0xFF9E80) : HEX(0xB3261E);
	}
	return T.text;
}

static int firstRunAt(const Doc* d, int y) {
	int lo = 0, hi = d->nRuns;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (d->runs[mid].y + 40 < y) lo = mid + 1;
		else hi = mid;
	}
	return lo;
}

static void drawField(Canvas* c, const Doc* d, const DocField* f, int sx, int sy, bool focus) {
	switch (f->type) {
		case FT_SUBMIT:
			cvRRect(c, sx, sy, f->w, f->h, f->h / 2, T.accent);
			cvTextFit(c, FONT_TEXT, sx + 9, sy + (f->h - fontHeight(FONT_TEXT)) / 2, f->w - 18, T.onAccent, f->value);
			break;
		case FT_CHECKBOX:
		case FT_RADIO:
			if (f->type == FT_RADIO) {
				cvRing(c, sx + 7, sy + 7, 7, 2, T.text2);
				if (f->checked) cvCircle(c, sx + 7, sy + 7, 4, T.accent);
			} else {
				cvRRectBorder(c, sx, sy, 14, 14, 3, 2, T.text2);
				if (f->checked) {
					cvRRect(c, sx, sy, 14, 14, 3, T.accent);
					cvIcon(c, IC_CHECK_14, sx, sy, T.onAccent);
				}
			}
			break;
		default: {
			cvRRect(c, sx, sy, f->w, f->h, 6, T.surface);
			cvRRectBorder(c, sx, sy, f->w, f->h, 6, 1, T.line);
			const char* v = f->value ? f->value : "";
			char stars[64];
			if (f->type == FT_PASSWORD) {
				int n = MIN((int)strlen(v), 60);
				memset(stars, '*', n);
				stars[n] = 0;
				v = stars;
			}
			cvSetClip(c, MAX(c->cx0, sx + 4), c->cy0, MIN(f->w - 8, c->cx1 - sx - 4), c->cy1 - c->cy0);
			int saveY0 = c->cy0, saveY1 = c->cy1;
			(void)saveY0;
			(void)saveY1;
			cvTextFit(c, FONT_TEXT, sx + 5, sy + (MIN(f->h, 22) - fontHeight(FONT_TEXT)) / 2, f->w - 10, T.text, v);
			break;
		}
	}
	if (focus) cvRRectBorder(c, sx - 3, sy - 3, f->w + 6, f->h + 6, 8, 2, uiSelColor());
}

void docDraw(Canvas* c, const Doc* d, int scroll, int viewY, int viewH, int focus) {
	cvSetClip(c, 0, viewY, SCR_W, viewH);
	int focusLink = -1, focusField = -1;
	if (focus >= 0 && focus < d->nFocus) {
		if (d->focus[focus].kind == FK_LINK) focusLink = d->focus[focus].idx;
		else focusField = d->focus[focus].idx;
	}
	for (int i = firstRunAt(d, scroll); i < d->nRuns; i++) {
		const DocRun* r = &d->runs[i];
		int sy = viewY + r->y - scroll;
		if (sy >= viewY + viewH) break;
		if (sy + r->h < viewY) continue;
		int sx = DOC_MARGIN + r->x;
		if (r->flags & RF_RULE) {
			cvHLine(c, sx, sy, r->w, T.line);
			continue;
		}
		if (focusLink >= 0 && r->link == focusLink) cvRRectA(c, sx - 2, sy, r->w + 4, r->h, 4, T.accent, 8);
		u16 col = docColor(r->color);
		cvTextN(c, r->font, sx, sy, col, d->pool + r->off, r->len);
		if (r->flags & RF_UNDER) cvHLine(c, sx, sy + fontAscent(r->font) + 2, r->w, lerpColor(col, T.bg, 110));
		if (focusLink >= 0 && r->link == focusLink) cvRRectBorder(c, sx - 3, sy - 1, r->w + 6, r->h + 2, 5, 2, uiSelColor());
	}
	for (int i = 0; i < d->nImgs; i++) {
		const DocImg* im = &d->imgs[i];
		if (!im->placed || !im->px) continue;
		int sy = viewY + im->y - scroll;
		if (sy >= viewY + viewH || sy + im->h < viewY) continue;
		cvImage(c, im->px, im->w, im->h, DOC_MARGIN + im->x, sy);
		if (focusLink >= 0 && im->link == focusLink) cvRRectBorder(c, DOC_MARGIN + im->x - 3, sy - 3, im->w + 6, im->h + 6, 5, 2, uiSelColor());
	}
	for (int i = 0; i < d->nFields; i++) {
		const DocField* f = &d->fields[i];
		if (f->type == FT_HIDDEN) continue;
		int sy = viewY + f->y - scroll;
		if (sy >= viewY + viewH || sy + f->h < viewY) continue;
		cvSetClip(c, 0, viewY, SCR_W, viewH);
		drawField(c, d, f, DOC_MARGIN + f->x, sy, i == focusField);
	}
	cvResetClip(c);
}

int docHit(const Doc* d, int px, int py) {
	px -= DOC_MARGIN;
	for (int i = 0; i < d->nFields; i++) {
		const DocField* f = &d->fields[i];
		if (f->type == FT_HIDDEN) continue;
		if (inRect(px, py, f->x - 3, f->y - 3, f->w + 6, f->h + 6)) {
			for (int k = 0; k < d->nFocus; k++)
				if (d->focus[k].kind == FK_FIELD && d->focus[k].idx == i) return k;
		}
	}
	int best = -1;
	for (int i = 0; i < d->nImgs && best < 0; i++) {
		const DocImg* im = &d->imgs[i];
		if (im->placed && im->link >= 0 && inRect(px, py, im->x, im->y, im->w, im->h)) best = im->link;
	}
	for (int i = firstRunAt(d, py - 30); i < d->nRuns && best < 0; i++) {
		const DocRun* r = &d->runs[i];
		if (r->y > py + 8) break;
		if (r->link < 0) continue;
		if (inRect(px, py, r->x - 4, r->y - 3, r->w + 8, r->h + 6)) {
			best = r->link;
			break;
		}
	}
	if (best < 0) return -1;
	for (int k = 0; k < d->nFocus; k++)
		if (d->focus[k].kind == FK_LINK && d->focus[k].idx == best) return k;
	return -1;
}

void docFocusBox(const Doc* d, int f, int* y0, int* y1) {
	*y0 = *y1 = 0;
	if (f < 0 || f >= d->nFocus) return;
	const DocFocus* fo = &d->focus[f];
	if (fo->kind == FK_FIELD) {
		*y0 = d->fields[fo->idx].y;
		*y1 = *y0 + d->fields[fo->idx].h;
		return;
	}
	int a = 1 << 30, b = 0;
	for (int i = 0; i < d->nImgs; i++)
		if (d->imgs[i].placed && d->imgs[i].link == fo->idx) {
			a = MIN(a, d->imgs[i].y);
			b = MAX(b, d->imgs[i].y + d->imgs[i].h);
		}
	for (int i = 0; i < d->nRuns; i++) {
		if (d->runs[i].link != fo->idx) continue;
		a = MIN(a, d->runs[i].y);
		b = MAX(b, d->runs[i].y + d->runs[i].h);
		if (b - a > 200) break;
	}
	if (a > b) a = b = 0;
	*y0 = a;
	*y1 = b;
}

const char* docFocusHref(const Doc* d, int f) {
	if (f < 0 || f >= d->nFocus || d->focus[f].kind != FK_LINK) return NULL;
	return d->pool + d->links[d->focus[f].idx];
}

static int appendEnc(char* out, int o, int sz, const char* s) {
	char tmp[1024];
	urlEncode(s, tmp, sizeof(tmp));
	int n = snprintf(out + o, sz - o, "%s", tmp);
	return MIN(sz - 1, o + n);
}

int docBuildSubmit(const Doc* d, int form, int submitField, char* out, int sz, bool* post) {
	int o = 0;
	out[0] = 0;
	*post = (form >= 0 && form < d->nForms) ? d->forms[form].post : false;
	for (int i = 0; i < d->nFields; i++) {
		const DocField* f = &d->fields[i];
		if (f->form != form) continue;
		const char* nm = d->pool + f->nameOff;
		if (!nm[0]) continue;
		if (f->type == FT_SUBMIT && i != submitField) continue;
		if ((f->type == FT_CHECKBOX || f->type == FT_RADIO) && !f->checked) continue;
		if (o) o = MIN(sz - 1, o + snprintf(out + o, sz - o, "&"));
		o = appendEnc(out, o, sz, nm);
		o = MIN(sz - 1, o + snprintf(out + o, sz - o, "="));
		o = appendEnc(out, o, sz, f->value ? f->value : "");
	}
	return o;
}

// ---------------------------------------------------------------------------
// HTML/XML -> texto puro (resumos de RSS): remove tags e CDATA, decodifica entidades
// ---------------------------------------------------------------------------
int htmlToText(const char* in, int n, char* out, int sz, bool latin1) {
	int o = 0;
	bool sp = true;
	const char* e = in + n;
	while (in < e && o < sz - 8) {
		if (!strncmp(in, "<![CDATA[", 9)) {
			in += 9;
			continue;
		}
		if (!strncmp(in, "]]>", 3)) {
			in += 3;
			continue;
		}
		if (*in == '<') {
			bool br = !strncasecmp(in, "<br", 3) || !strncasecmp(in, "<p", 2) || !strncasecmp(in, "</p", 3) || !strncasecmp(in, "<li", 3);
			const char* g = memchr(in, '>', e - in);
			in = g ? g + 1 : e;
			if (br && o > 0 && out[o - 1] != '\n') {
				if (o > 0 && out[o - 1] == ' ') o--;
				out[o++] = '\n';
				sp = true;
			}
			continue;
		}
		if (*in == '&') {
			char tmp[8];
			int tl = 0;
			int k = decodeEntity(in, tmp, &tl);
			if (k) {
				in += k;
				if (tl == 2 && (u8)tmp[0] == 0xC2 && (u8)tmp[1] == 0xA0) {
					tmp[0] = ' ';
					tl = 1;
				}
				// entidade pode conter tags escapadas (&lt;p&gt;): trata '<' decodificado como tag
				if (tl == 1 && tmp[0] == '<') {
					const char* g = strstr(in, "&gt;");
					if (g && g - in < 200) {
						in = g + 4;
						continue;
					}
				}
				if (tl == 1 && tmp[0] == ' ') {
					if (!sp) out[o++] = ' ';
					sp = true;
					continue;
				}
				memcpy(out + o, tmp, tl);
				o += tl;
				sp = false;
				continue;
			}
		}
		u8 c = (u8)*in++;
		if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
			if (!sp) out[o++] = ' ';
			sp = true;
			continue;
		}
		if (latin1 && c >= 0x80) {
			o += putUtf8(out + o, c < 0xA0 ? CP1252[c - 0x80] : c);
		} else {
			out[o++] = c;
		}
		sp = false;
	}
	while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\n')) o--;
	out[o] = 0;
	return o;
}

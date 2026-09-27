// DSi Dash — Arquivos: navegador do cartao SD, abre .nds (Unlaunch), imagens e textos
#include "common.h"
#include "image.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ENT 512

typedef struct Ent {
	char* name;
	bool dir;
	long size;
	time_t mtime;
} Ent;

static char s_cwd[256];
static Ent s_ent[MAX_ENT];
static int s_n;
static ListView s_lv;
static char s_msg[96];

// previa do item selecionado
static int s_prevFor = -1;
static u16* s_prevImg;
static int s_prevW, s_prevH;
static u16 s_ndsIcon[32 * 32];
static bool s_hasIcon;
static char s_ndsTitle[3][64];

static bool hasExt(const char* n, const char* e) {
	const char* d = strrchr(n, '.');
	return d && !strcasecmp(d, e);
}

static bool isNds(const char* n) { return hasExt(n, ".nds") || hasExt(n, ".dsi") || hasExt(n, ".srl"); }
static bool isText(const char* n) {
	return hasExt(n, ".txt") || hasExt(n, ".ini") || hasExt(n, ".md") || hasExt(n, ".log") || hasExt(n, ".json") || hasExt(n, ".xml") ||
		hasExt(n, ".cfg") || hasExt(n, ".csv") || hasExt(n, ".htm") || hasExt(n, ".html");
}

static int cmpEnt(const void* a, const void* b) {
	const Ent* x = a;
	const Ent* y = b;
	if (x->dir != y->dir) return x->dir ? -1 : 1;
	return strcasecmp(x->name, y->name);
}

static void freeEnts(void) {
	for (int i = 0; i < s_n; i++) free(s_ent[i].name);
	s_n = 0;
}

static void drawRow(Canvas* c, ListView* lv, int i, int x, int y, int w, int h, bool sel);

static void load(void) {
	freeEnts();
	s_prevFor = -1;
	DIR* d = opendir(s_cwd);
	if (d) {
		struct dirent* e;
		while ((e = readdir(d)) && s_n < MAX_ENT) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			Ent* en = &s_ent[s_n++];
			en->name = strdup(e->d_name);
			en->dir = e->d_type == DT_DIR;
			en->size = 0;
			en->mtime = 0;
			if (!en->dir) {
				char p[512];
				struct stat st;
				snprintf(p, sizeof(p), "%s%s", s_cwd, e->d_name);
				if (!stat(p, &st)) {
					en->size = st.st_size;
					en->mtime = st.st_mtime;
				}
			}
		}
		closedir(d);
	}
	qsort(s_ent, s_n, sizeof(Ent), cmpEnt);
	lvInit(&s_lv, 0, 26, SCR_W, HINTS_Y - 28, 24, s_n, drawRow);
	gfxInvalidate(GFX_BOTH);
}

static void fullPath(int i, char* out, int sz) { snprintf(out, sz, "%s%s", s_cwd, s_ent[i].name); }

// icone e titulo do banner do .nds
static void readBanner(const char* path) {
	s_hasIcon = false;
	memset(s_ndsTitle, 0, sizeof(s_ndsTitle));
	FILE* f = fopen(path, "rb");
	if (!f) return;
	u32 off = 0;
	fseek(f, 0x68, SEEK_SET);
	if (fread(&off, 4, 1, f) == 1 && off) {
		u8* b = (u8*)malloc(0x840);
		if (b && !fseek(f, off, SEEK_SET) && fread(b, 1, 0x840, f) == 0x840) {
			const u16* pal = (const u16*)(b + 0x220);
			for (int ty = 0; ty < 4; ty++)
				for (int tx = 0; tx < 4; tx++)
					for (int py = 0; py < 8; py++)
						for (int px = 0; px < 8; px++) {
							u8 v = b[0x20 + (ty * 4 + tx) * 32 + py * 4 + px / 2];
							int ci = (px & 1) ? v >> 4 : v & 15;
							s_ndsIcon[(ty * 8 + py) * 32 + tx * 8 + px] = ci ? (pal[ci] | 0x8000) : 0;
						}
			s_hasIcon = true;
			// titulo em ingles (0x340): ate 3 linhas separadas por \n
			const u16* t = (const u16*)(b + 0x340);
			int line = 0, n = 0;
			for (int i = 0; i < 128 && t[i] && line < 3; i++) {
				if (t[i] == '\n') {
					line++;
					n = 0;
					continue;
				}
				char tmp[4];
				int l = 0;
				u16 ch = t[i];
				if (ch < 0x80) tmp[l++] = ch;
				else if (ch < 0x800) {
					tmp[l++] = 0xC0 | (ch >> 6);
					tmp[l++] = 0x80 | (ch & 0x3F);
				} else {
					tmp[l++] = 0xE0 | (ch >> 12);
					tmp[l++] = 0x80 | ((ch >> 6) & 0x3F);
					tmp[l++] = 0x80 | (ch & 0x3F);
				}
				if (n + l < 63) {
					memcpy(s_ndsTitle[line] + n, tmp, l);
					n += l;
				}
			}
		}
		free(b);
	}
	fclose(f);
}

static void preview(void) {
	int i = s_lv.sel;
	if (i == s_prevFor || i >= s_n) return;
	s_prevFor = i;
	free(s_prevImg);
	s_prevImg = NULL;
	s_hasIcon = false;
	if (s_ent[i].dir) return;
	char p[512];
	fullPath(i, p, sizeof(p));
	if (isNds(s_ent[i].name)) readBanner(p);
	else if (imgIsImageName(s_ent[i].name) && s_ent[i].size < 3 * 1024 * 1024) s_prevImg = imgLoadFile(p, 236, 120, &s_prevW, &s_prevH);
}

static void goUp(void) {
	if (!strcmp(s_cwd, sysRoot())) {
		sndBack();
		appHome();
		return;
	}
	int n = strlen(s_cwd);
	if (n > 0) s_cwd[n - 1] = 0;
	char* sl = strrchr(s_cwd, '/');
	char last[128] = {0};
	if (sl) {
		snprintf(last, sizeof(last), "%s", sl + 1);
		sl[1] = 0;
	}
	load();
	for (int i = 0; i < s_n; i++)
		if (!strcmp(s_ent[i].name, last)) {
			s_lv.sel = i;
			lvEnsureVisible(&s_lv);
		}
	sndBack();
}

static char s_pendingNds[512];

static void launchChoice(int c) {
	if (c != 0) return;
	if (!sysRebootTo(s_pendingNds)) uiToast("Abrir .nds exige o modo DSi com Unlaunch");
}

static void delChoice(int c) {
	if (c != 0) return;
	char p[512];
	fullPath(s_lv.sel, p, sizeof(p));
	int r = s_ent[s_lv.sel].dir ? rmdir(p) : remove(p);
	uiToast(r == 0 ? "Apagado" : (s_ent[s_lv.sel].dir ? "A pasta precisa estar vazia" : "N\xC3\xA3o foi poss\xC3\xADvel apagar"));
	int sel = s_lv.sel;
	load();
	s_lv.sel = MIN(sel, MAX(0, s_n - 1));
	lvEnsureVisible(&s_lv);
}

static void onNewFolder(const char* name) {
	if (!name || !name[0]) return;
	char p[512];
	snprintf(p, sizeof(p), "%s%s", s_cwd, name);
	uiToast(mkdir(p, 0777) == 0 ? "Pasta criada" : "Erro ao criar pasta");
	load();
}

static void onRename(const char* name) {
	if (!name || !name[0] || s_lv.sel >= s_n) return;
	char a[512], b[512];
	fullPath(s_lv.sel, a, sizeof(a));
	snprintf(b, sizeof(b), "%s%s", s_cwd, name);
	uiToast(rename(a, b) == 0 ? "Renomeado" : "Erro ao renomear");
	load();
}

static void moreChoice(int c) {
	if (c == 0) kbdOpen("Nome da nova pasta", "", 60, onNewFolder);
	else if (c == 1 && s_n) kbdOpen("Novo nome", s_ent[s_lv.sel].name, 120, onRename);
	else if (c == 2 && s_n) {
		static const char* opts[] = {"Apagar", "Cancelar"};
		uiDialog("Apagar?", s_ent[s_lv.sel].name, opts, 2, delChoice);
	}
}

static void open_(int i) {
	char p[512];
	fullPath(i, p, sizeof(p));
	Ent* e = &s_ent[i];
	if (e->dir) {
		snprintf(s_cwd, sizeof(s_cwd), "%s/", p);
		load();
		sndClick();
		return;
	}
	if (isNds(e->name)) {
		snprintf(s_pendingNds, sizeof(s_pendingNds), "%s", p);
		static const char* opts[] = {"Abrir", "Cancelar"};
		char m[160];
		snprintf(m, sizeof(m), "O DSi vai reiniciar e abrir %s.", e->name);
		uiDialog(s_ndsTitle[0][0] ? s_ndsTitle[0] : e->name, m, opts, 2, launchChoice);
		return;
	}
	if (isText(e->name)) {
		char url[600];
		snprintf(url, sizeof(url), "file:%s", p);
		browserOpenUrl(url);
		return;
	}
	if (imgIsImageName(e->name)) {
		appOpen(APP_ALBUM);
		return;
	}
	uiToast("Tipo de arquivo sem visualizador");
}

static void fEnter(void) {
	if (!s_cwd[0] || strncmp(s_cwd, sysRoot(), strlen(sysRoot()))) snprintf(s_cwd, sizeof(s_cwd), "%s", sysRoot());
	if (!sysHasStorage()) {
		snprintf(s_msg, sizeof(s_msg), "Nenhum cart\xC3\xA3o SD encontrado");
		s_n = 0;
		return;
	}
	s_msg[0] = 0;
	int sel = s_lv.sel;
	load();
	s_lv.sel = MIN(sel, MAX(0, s_n - 1));
	lvEnsureVisible(&s_lv);
}

static void fLeave(void) {
	free(s_prevImg);
	s_prevImg = NULL;
	s_prevFor = -1;
}

static void fFrame(void) {
	if (g_in.down & KEY_B) {
		if (!sysHasStorage()) {
			appHome();
			return;
		}
		goUp();
		return;
	}
	if (!sysHasStorage()) return;
	int old = s_lv.sel;
	int act = lvUpdate(&s_lv);
	if (old != s_lv.sel) sndMove();
	if (act >= 0 && act < s_n) open_(act);
	if (g_in.down & KEY_Y && s_n) {
		static const char* opts[] = {"Apagar", "Cancelar"};
		uiDialog("Apagar?", s_ent[s_lv.sel].name, opts, 2, delChoice);
	}
	if (g_in.down & KEY_X) {
		static const char* opts[] = {"Nova pasta", "Renomear", "Apagar", "Cancelar"};
		uiDialog("Arquivo", s_n ? s_ent[s_lv.sel].name : "", opts, 4, moreChoice);
	}
	preview();
	gfxInvalidate(GFX_BOT);
	if (old != s_lv.sel) gfxInvalidate(GFX_TOP);
}

static int iconFor(const Ent* e) {
	if (e->dir) return IC_FILES_14;
	if (imgIsImageName(e->name)) return IC_IMAGE_14;
	return IC_FILE_14;
}

static void drawRow(Canvas* c, ListView* lv, int i, int x, int y, int w, int h, bool sel) {
	const Ent* e = &s_ent[i];
	if (sel) cvRRect(c, x + 4, y + 1, w - 8, h - 2, 6, T.surface);
	u16 ic = e->dir ? HEX(0xF4A300) : (isNds(e->name) ? HEX(0xE0457B) : T.text2);
	if (isNds(e->name)) cvIcon(c, IC_GAMES_20, x + 10, y + 2, ic);
	else cvIcon(c, iconFor(e), x + 13, y + 5, ic);
	cvTextFit(c, FONT_BODY, x + 36, y + 2, w - 110, T.text, e->name);
	if (!e->dir) {
		char sz[24];
		if (e->size < 1024) snprintf(sz, sizeof(sz), "%ld B", e->size);
		else if (e->size < 1024 * 1024) snprintf(sz, sizeof(sz), "%ld KB", e->size / 1024);
		else snprintf(sz, sizeof(sz), "%ld.%ld MB", e->size / 1048576, (e->size % 1048576) * 10 / 1048576);
		cvTextR(c, FONT_SMALL, x + w - 14, y + 4, T.text2, sz);
	}
	if (sel) cvRRectBorder(c, x + 2, y - 1, w - 4, h + 2, 8, 2, uiSelColor());
}

static void fDrawTop(Canvas* c) {
	cvClear(c, T.bg);
	uiStatusBar(c, false);
	uiHeader(c, IC_FILES_20, "Arquivos");
	if (!sysHasStorage()) {
		uiEmpty(c, IC_FILES_40, s_msg, NULL);
		return;
	}
	int y = STATUS_H + 34;
	if (!s_n) {
		cvTextC(c, FONT_BODY, SCR_W / 2, 90, T.text2, "Pasta vazia");
		return;
	}
	const Ent* e = &s_ent[s_lv.sel];
	if (isNds(e->name) && s_hasIcon && s_prevFor == s_lv.sel) {
		// icone ampliado 2x
		for (int py = 0; py < 64; py++)
			for (int px = 0; px < 64; px++) {
				u16 v = s_ndsIcon[(py / 2) * 32 + px / 2];
				if (v) c->px[(y + py) * SCR_W + 12 + px] = v;
			}
		for (int l = 0; l < 3; l++) cvTextFit(c, l ? FONT_BODY : FONT_TITLE, 86, y + 2 + l * 20, SCR_W - 96, l ? T.text2 : T.text, s_ndsTitle[l]);
		cvTextWrap(c, FONT_SMALL, 12, y + 74, SCR_W - 24, 3, T.text2,
			sysIsDSi() ? "A: reinicia o DSi e abre este app (precisa do Unlaunch)." : "Abrir .nds precisa do modo DSi com Unlaunch.");
		return;
	}
	if (s_prevImg && s_prevFor == s_lv.sel) {
		cvImage(c, s_prevImg, s_prevW, s_prevH, (SCR_W - s_prevW) / 2, y);
		y += s_prevH + 6;
	}
	cvTextFit(c, FONT_TITLE, 12, y, SCR_W - 24, T.text, e->name);
	y += 24;
	char b[128];
	if (e->dir) snprintf(b, sizeof(b), "Pasta");
	else if (e->mtime) {
		struct tm t;
		time_t m = e->mtime;
		gmtime_r(&m, &t);
		snprintf(b, sizeof(b), "%ld bytes  \xE2\x80\xA2  %02d/%02d/%04d %02d:%02d", e->size, t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min);
	} else snprintf(b, sizeof(b), "%ld bytes", e->size);
	cvText(c, FONT_SMALL, 12, y, T.text2, b);
}

static void fDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	uiBackButton(c);
	const char* shown = s_cwd + strlen(sysRoot()) - 1;
	cvTextFit(c, FONT_BODY, 36, 4, SCR_W - 44, T.text, (!sysHasStorage() || !shown[1]) ? "Cart\xC3\xA3o SD" : shown);
	lvDraw(c, &s_lv);
	static const Hint h[] = {{KEY_X, "Mais"}, {KEY_Y, "Apagar"}, {KEY_A, "Abrir"}, {KEY_B, "Voltar"}};
	uiHints(c, h, 4);
}

const App app_files = {"Arquivos", fEnter, fLeave, fFrame, fDrawTop, fDrawBot};

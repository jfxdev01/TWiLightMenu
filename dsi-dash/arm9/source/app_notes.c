// DSi Dash — Notas (arquivos .txt em _nds/dsidash/notas no cartao SD)
#include "common.h"
#include <dirent.h>
#include <sys/stat.h>

#define MAX_NOTES 100

typedef struct Note {
	char file[32];
	char* text;
	time_t mtime;
} Note;

static Note s_notes[MAX_NOTES];
static int s_n;
static ListView s_lv;
static int s_editing = -1;

static void notesDir(char* out, int sz) { sysDataPath(out, sz, "notas"); }

static char* readAll(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return strdup("");
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n > 4000) n = 4000;
	char* b = (char*)malloc(n + 1);
	if (b) {
		n = fread(b, 1, n, f);
		b[n] = 0;
	}
	fclose(f);
	return b ? b : strdup("");
}

static int cmpNote(const void* a, const void* b) {
	const Note* x = a;
	const Note* y = b;
	return x->mtime < y->mtime ? 1 : (x->mtime > y->mtime ? -1 : strcmp(y->file, x->file));
}

static void drawRow(Canvas* c, ListView* lv, int i, int x, int y, int w, int h, bool sel);

static void load(void) {
	for (int i = 0; i < s_n; i++) free(s_notes[i].text);
	s_n = 0;
	char dir[96];
	notesDir(dir, sizeof(dir));
	mkdir(dir, 0777);
	DIR* d = opendir(dir);
	if (d) {
		struct dirent* e;
		while ((e = readdir(d)) && s_n < MAX_NOTES) {
			if (e->d_type == DT_DIR || !strcasestr(e->d_name, ".txt")) continue;
			Note* n = &s_notes[s_n];
			snprintf(n->file, sizeof(n->file), "%s", e->d_name);
			char p[160];
			snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
			struct stat st;
			n->mtime = stat(p, &st) ? 0 : st.st_mtime;
			n->text = readAll(p);
			s_n++;
		}
		closedir(d);
	}
	qsort(s_notes, s_n, sizeof(Note), cmpNote);
	int sel = s_lv.sel;
	lvInit(&s_lv, 0, 26, SCR_W, HINTS_Y - 28, 34, s_n + 1, drawRow);
	s_lv.sel = MIN(sel, s_n);
	gfxInvalidate(GFX_BOTH);
}

bool notesAdd(const char* text) {
	if (!sysHasStorage()) return false;
	char dir[96], p[160];
	notesDir(dir, sizeof(dir));
	mkdir(dir, 0777);
	for (int k = 1; k < 10000; k++) {
		snprintf(p, sizeof(p), "%s/nota_%04d.txt", dir, k);
		FILE* t = fopen(p, "rb");
		if (t) {
			fclose(t);
			continue;
		}
		FILE* f = fopen(p, "wb");
		if (!f) return false;
		fputs(text, f);
		fclose(f);
		return true;
	}
	return false;
}

static void onEdit(const char* text) {
	if (!text) return;
	char dir[96], p[160];
	notesDir(dir, sizeof(dir));
	if (s_editing < 0) {
		if (text[0]) notesAdd(text);
	} else {
		snprintf(p, sizeof(p), "%s/%s", dir, s_notes[s_editing].file);
		FILE* f = fopen(p, "wb");
		if (f) {
			fputs(text, f);
			fclose(f);
		}
	}
	load();
}

static void delChoice(int c) {
	if (c != 0 || s_lv.sel == 0 || s_lv.sel > s_n) return;
	char dir[96], p[160];
	notesDir(dir, sizeof(dir));
	snprintf(p, sizeof(p), "%s/%s", dir, s_notes[s_lv.sel - 1].file);
	remove(p);
	load();
	uiToast("Nota apagada");
}

static void drawRow(Canvas* c, ListView* lv, int i, int x, int y, int w, int h, bool sel) {
	cvRRect(c, x + 6, y + 2, w - 12, h - 4, 8, sel ? T.surface2 : T.surface);
	if (i == 0) {
		cvText(c, FONT_BODY, x + 16, y + 7, T.accent, "+ Nova nota");
	} else {
		const Note* n = &s_notes[i - 1];
		char first[96];
		int k = 0;
		while (n->text[k] && n->text[k] != '\n' && k < 95) {
			first[k] = n->text[k];
			k++;
		}
		first[k] = 0;
		cvTextFit(c, FONT_BODY, x + 16, y + 7, w - 32, T.text, first[0] ? first : "(vazia)");
	}
	if (sel) cvRRectBorder(c, x + 4, y, w - 8, h, 10, 2, uiSelColor());
}

static void nEnter(void) {
	if (sysHasStorage()) load();
}

static void nLeave(void) {
	for (int i = 0; i < s_n; i++) free(s_notes[i].text);
	s_n = 0;
}

static void nFrame(void) {
	if (g_in.down & KEY_B) {
		sndBack();
		appHome();
		return;
	}
	if (!sysHasStorage()) return;
	int old = s_lv.sel;
	int act = lvUpdate(&s_lv);
	if (old != s_lv.sel) sndMove();
	if (act == 0 || (g_in.down & KEY_X)) {
		s_editing = -1;
		kbdOpen("Nova nota", "", 500, onEdit);
	} else if (act > 0) {
		s_editing = act - 1;
		kbdOpen("Editar nota", s_notes[act - 1].text, 500, onEdit);
	}
	if ((g_in.down & KEY_Y) && s_lv.sel > 0) {
		static const char* opts[] = {"Apagar", "Cancelar"};
		uiDialog("Apagar esta nota?", NULL, opts, 2, delChoice);
	}
	gfxInvalidate(GFX_BOT);
}

static void nDrawTop(Canvas* c) {
	cvClear(c, T.bg);
	uiStatusBar(c, false);
	uiHeader(c, IC_NOTES_20, "Notas");
	if (!sysHasStorage()) {
		uiEmpty(c, IC_NOTES_40, "Sem cart\xC3\xA3o SD", NULL);
		return;
	}
	if (s_lv.sel == 0 || s_lv.sel > s_n) {
		uiEmpty(c, IC_NOTES_40, s_n ? "Escolha uma nota" : "Nenhuma nota", "Toque em \"+ Nova nota\" para escrever. Textos lidos em QR codes tamb\xC3\xA9m aparecem aqui.");
		return;
	}
	cvRRect(c, 8, STATUS_H + 32, SCR_W - 16, SCR_H - STATUS_H - 40, 10, T.surface);
	cvTextWrap(c, FONT_TEXT, 16, STATUS_H + 38, SCR_W - 32, 7, T.text, s_notes[s_lv.sel - 1].text);
}

static void nDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	uiBackButton(c);
	cvTextC(c, FONT_TITLE, SCR_W / 2, 3, T.text, "Notas");
	lvDraw(c, &s_lv);
	static const Hint h[] = {{KEY_Y, "Apagar"}, {KEY_A, "Editar"}, {KEY_B, "Voltar"}};
	uiHints(c, h, 3);
}

const App app_notes = {"Notas", nEnter, nLeave, nFrame, nDrawTop, nDrawBot};

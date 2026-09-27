// DSi Dash — Album: fotos do cartao SD (DCIM e Downloads)
#include "common.h"
#include <malloc.h>
#include "image.h"
#include <dirent.h>
#include <sys/stat.h>

#define MAX_PHOTOS 300
#define TW 58
#define TH 44
#define COLS 4
#define ROWS 3
#define GX0 8
#define GY0 26
#define GSX 60
#define GSY 48

typedef struct Photo {
	char* path;
	time_t mtime;
	long size;
	u16* thumb;
	volatile u8 state;  // 0 pendente, 1 ok, 2 erro
} Photo;

static Photo s_ph[MAX_PHOTOS];
static int s_n;
static int s_sel, s_rowTop;
static bool s_scanned;
static NetJob s_job;
static volatile int s_wantBig = -1;
static int s_bigIdx = -1;
static u16* s_big;
static int s_bigW, s_bigH;
static volatile bool s_bigReady;
static u16* s_newBig;
static int s_newBigW, s_newBigH, s_newBigIdx;

static void addDir(const char* dir) {
	DIR* d = opendir(dir);
	if (!d) return;
	struct dirent* e;
	while ((e = readdir(d)) && s_n < MAX_PHOTOS) {
		if (e->d_name[0] == '.') continue;
		char p[256];
		snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
		if (e->d_type == DT_DIR) continue;
		if (!imgIsImageName(e->d_name)) continue;
		struct stat st;
		Photo* ph = &s_ph[s_n];
		memset(ph, 0, sizeof(*ph));
		ph->path = strdup(p);
		if (!stat(p, &st)) {
			ph->mtime = st.st_mtime;
			ph->size = st.st_size;
		}
		s_n++;
	}
	closedir(d);
}

static int cmpPhoto(const void* a, const void* b) {
	const Photo* x = a;
	const Photo* y = b;
	if (x->mtime != y->mtime) return x->mtime < y->mtime ? 1 : -1;
	return -strcmp(x->path, y->path);
}

static void scan(void) {
	for (int i = 0; i < s_n; i++) {
		free(s_ph[i].path);
		free(s_ph[i].thumb);
	}
	s_n = 0;
	if (!sysHasStorage()) return;
	char base[64];
	snprintf(base, sizeof(base), "%sDCIM", sysRoot());
	DIR* d = opendir(base);
	if (d) {
		struct dirent* e;
		char sub[16][96];
		int ns = 0;
		while ((e = readdir(d)) && ns < 16) {
			if (e->d_name[0] == '.' || e->d_type != DT_DIR) continue;
			snprintf(sub[ns++], 96, "%s/%s", base, e->d_name);
		}
		closedir(d);
		for (int i = 0; i < ns; i++) addDir(sub[i]);
		addDir(base);
	}
	snprintf(base, sizeof(base), "%sDownloads", sysRoot());
	addDir(base);
	qsort(s_ph, s_n, sizeof(Photo), cmpPhoto);
	s_scanned = true;
}

// thread de rede: carrega a imagem grande pedida e depois as miniaturas
static void albumJob(NetJob* j) {
	for (;;) {
		if (j->cancel) return;
		int want = s_wantBig;
		if (want >= 0 && want != s_newBigIdx && !s_bigReady) {
			int w, h;
			u16* img = imgLoadFile(s_ph[want].path, SCR_W, SCR_H, &w, &h);
			s_newBig = img;
			s_newBigW = w;
			s_newBigH = h;
			s_newBigIdx = want;
			s_bigReady = true;
			continue;
		}
		// proxima miniatura (a partir da selecao)
		int next = -1;
		for (int k = 0; k < s_n; k++) {
			int i = (s_sel + k) % s_n;
			if (s_ph[i].state == 0) {
				next = i;
				break;
			}
		}
		if (next < 0) return;
		int w, h;
		u16* t = imgLoadFile(s_ph[next].path, TW, TH, &w, &h);
		if (t && (w != TW || h != TH)) {
			// centraliza numa caixa TW x TH
			u16* box = (u16*)malloc(TW * TH * 2);
			if (box) {
				for (int i = 0; i < TW * TH; i++) box[i] = COL8(20, 20, 20);
				int ox = (TW - w) / 2, oy = (TH - h) / 2;
				for (int y = 0; y < h; y++) memcpy(box + (oy + y) * TW + ox, t + y * w, w * 2);
			}
			free(t);
			t = box;
		}
		s_ph[next].thumb = t;
		s_ph[next].state = t ? 1 : 2;
	}
}

static void kick(void) {
	if (!jobBusy(&s_job) && s_n) {
		s_job.run = albumJob;
		netSubmit(&s_job);
	}
}

static void alEnter(void) {
	scan();
	s_sel = 0;
	s_rowTop = 0;
	s_bigIdx = -1;
	s_newBigIdx = -1;
	s_bigReady = false;
	s_wantBig = s_n ? 0 : -1;
	kick();
	gfxInvalidate(GFX_BOTH);
}

static void alLeave(void) {
	s_job.cancel = true;
	while (jobBusy(&s_job)) threadWaitForVBlank();
	s_job.state = JOB_IDLE;
	free(s_big);
	s_big = NULL;
	if (s_bigReady) {
		free(s_newBig);
		s_bigReady = false;
	}
	for (int i = 0; i < s_n; i++) {
		free(s_ph[i].path);
		free(s_ph[i].thumb);
	}
	s_n = 0;
}

static void delChoice(int c) {
	if (c != 0 || s_sel >= s_n) return;
	if (remove(s_ph[s_sel].path) == 0) {
		uiToast("Foto apagada");
		free(s_ph[s_sel].path);
		free(s_ph[s_sel].thumb);
		memmove(&s_ph[s_sel], &s_ph[s_sel + 1], (s_n - s_sel - 1) * sizeof(Photo));
		s_n--;
		if (s_sel >= s_n) s_sel = MAX(0, s_n - 1);
		s_wantBig = s_n ? s_sel : -1;
		s_bigIdx = -1;
		kick();
	} else {
		uiToast("N\xC3\xA3o foi poss\xC3\xADvel apagar");
	}
	gfxInvalidate(GFX_BOTH);
}

static void alFrame(void) {
	if (s_bigReady) {
		free(s_big);
		s_big = s_newBig;
		s_bigW = s_newBigW;
		s_bigH = s_newBigH;
		s_bigIdx = s_newBigIdx;
		s_newBig = NULL;
		s_bigReady = false;
		gfxInvalidate(GFX_TOP);
		kick();
	}
	if (s_job.state == JOB_DONE) {
		s_job.state = JOB_IDLE;
		gfxInvalidate(GFX_BOTH);
		if (s_wantBig >= 0 && s_wantBig != s_bigIdx) kick();
	}
	if (jobBusy(&s_job) && (g_frame % 10) == 0) gfxInvalidate(GFX_BOT);
	if (g_in.down & KEY_B) {
		sndBack();
		appHome();
		return;
	}
	if (!s_n) return;
	int old = s_sel;
	if (g_in.rep & KEY_RIGHT) s_sel = MIN(s_n - 1, s_sel + 1);
	if (g_in.rep & KEY_LEFT) s_sel = MAX(0, s_sel - 1);
	if (g_in.rep & KEY_DOWN) s_sel = MIN(s_n - 1, s_sel + COLS);
	if (g_in.rep & KEY_UP) s_sel = MAX(0, s_sel - COLS);
	if (g_in.rep & KEY_R) s_sel = MIN(s_n - 1, s_sel + 1);
	if (g_in.rep & KEY_L) s_sel = MAX(0, s_sel - 1);
	if (g_in.tap) {
		for (int r = 0; r < ROWS; r++)
			for (int c = 0; c < COLS; c++) {
				int i = (s_rowTop + r) * COLS + c;
				if (i < s_n && inRect(g_in.sx, g_in.sy, GX0 + c * GSX, GY0 + r * GSY, TW, TH)) s_sel = i;
			}
	}
	if (g_in.down & KEY_Y) {
		static const char* opts[] = {"Apagar", "Cancelar"};
		uiDialog("Apagar esta foto?", strrchr(s_ph[s_sel].path, '/') + 1, opts, 2, delChoice);
	}
	if (s_sel != old) {
		sndMove();
		int row = s_sel / COLS;
		if (row < s_rowTop) s_rowTop = row;
		if (row >= s_rowTop + ROWS) s_rowTop = row - ROWS + 1;
		s_wantBig = s_sel;
		kick();
		gfxInvalidate(GFX_BOTH);
	}
	gfxInvalidate(GFX_BOT);
}

static void alDrawTop(Canvas* c) {
	cvClear(c, HEX(0x000000));
	if (!s_n) {
		cvClear(c, T.bg);
		uiStatusBar(c, false);
		uiEmpty(c, IC_ALBUM_40, s_scanned && sysHasStorage() ? "Nenhuma foto ainda" : "Sem cart\xC3\xA3o SD", "Tire fotos com o app C\xC3\xA2mera. Imagens baixadas pelo navegador tamb\xC3\xA9m aparecem aqui.");
		return;
	}
	if (s_big && s_bigIdx == s_sel) {
		cvImage(c, s_big, s_bigW, s_bigH, (SCR_W - s_bigW) / 2, (SCR_H - s_bigH) / 2);
	} else {
		uiSpinner(c, SCR_W / 2, SCR_H / 2, 10, HEX(0xFFFFFF));
	}
	const Photo* p = &s_ph[s_sel];
	char info[128], d[32] = "";
	if (p->mtime) {
		struct tm t;
		time_t m = p->mtime;
		gmtime_r(&m, &t);
		snprintf(d, sizeof(d), "%02d/%02d/%04d %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min);
	}
	snprintf(info, sizeof(info), "%s  \xE2\x80\xA2  %s  \xE2\x80\xA2  %ld KB", strrchr(p->path, '/') + 1, d, (p->size + 1023) / 1024);
	cvFillA(c, 0, SCR_H - 18, SCR_W, 18, HEX(0x000000), 18);
	cvTextFit(c, FONT_SMALL, 6, SCR_H - 17, SCR_W - 12, HEX(0xFFFFFF), info);
}

static void alDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	uiBackButton(c);
	char t[48];
	snprintf(t, sizeof(t), "\xC3\x81lbum  (%d)", s_n);
	cvTextC(c, FONT_TITLE, SCR_W / 2, 3, T.text, t);
	for (int r = 0; r < ROWS; r++) {
		for (int col = 0; col < COLS; col++) {
			int i = (s_rowTop + r) * COLS + col;
			if (i >= s_n) break;
			int x = GX0 + col * GSX, y = GY0 + r * GSY;
			if (s_ph[i].state == 1 && s_ph[i].thumb) cvImage(c, s_ph[i].thumb, TW, TH, x, y);
			else {
				cvRRect(c, x, y, TW, TH, 4, T.surface2);
				if (s_ph[i].state == 2) cvIcon(c, IC_IMAGE_14, x + (TW - 14) / 2, y + (TH - 14) / 2, T.text2);
				else cvIconA(c, IC_IMAGE_14, x + (TW - 14) / 2, y + (TH - 14) / 2, T.text2, 12);
			}
			if (i == s_sel) cvRRectBorder(c, x - 3, y - 3, TW + 6, TH + 6, 5, 2, uiSelColor());
		}
	}
	if (!s_n) cvTextC(c, FONT_BODY, SCR_W / 2, 80, T.text2, "Nada por aqui");
	static const Hint h[] = {{KEY_Y, "Apagar"}, {KEY_B, "Voltar"}};
	uiHints(c, h, 2);
}

const App app_album = {"\xC3\x81lbum", alEnter, alLeave, alFrame, alDrawTop, alDrawBot};

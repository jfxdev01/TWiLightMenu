// DSi Dash Racing — app: menus, HUD sobre o 3D (tela de cima) e painel na tela de toque
// Durante o jogo o 3D fica na tela de cima (engine principal), entao:
//   g_bot (presente em BG_GFX = VRAM D) = HUD por cima do 3D, na tela de CIMA
//   g_top (presente em BG_GFX_SUB = VRAM C) = tela de BAIXO (toque)
#include "race.h"
#include <math.h>

static u64 s_lastTick;
static int s_menuSel;
static int s_pauseSel;
static int s_hudState = -1;
static struct {
	int place, lap, cs, kmh, boost, cd;
	char msg[48];
} s_hud;
static float s_bestEver;
static bool s_newRecord;
static u16* s_panelBg;

static const char* NAMES[NCARS] = {"Voc\xC3\xAA", "Rafa", "Bia", "L\xC3\xA9o", "Duda", "Caio"};
static const char* DIFF[3] = {"F\xC3\xA1" "cil", "Normal", "Dif\xC3\xAD" "cil"};
static const char* CAMS[CAM_COUNT] = {"Persegui\xC3\xA7\xC3\xA3o", "Distante", "Cap\xC3\xB4"};

// ---------------------------------------------------------------------------
static void fmtTime(char* o, int sz, float t) {
	if (t <= 0) {
		snprintf(o, sz, "-:--.--");
		return;
	}
	int cs = (int)(t * 100 + 0.5f);
	snprintf(o, sz, "%d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
}

static void loadBest(void) {
	s_bestEver = 0;
	if (!sysHasStorage()) return;
	char p[96];
	sysDataPath(p, sizeof(p), "corrida.ini");
	FILE* f = fopen(p, "r");
	if (!f) return;
	char l[64];
	while (fgets(l, sizeof(l), f)) {
		if (!strncmp(l, "melhor=", 7)) s_bestEver = atoi(l + 7) / 1000.0f;
		else if (!strncmp(l, "voltas=", 7)) R.laps = CLAMP(atoi(l + 7), 1, 9);
		else if (!strncmp(l, "dificuldade=", 12)) R.difficulty = CLAMP(atoi(l + 12), 0, 2);
		else if (!strncmp(l, "cor=", 4)) R.playerColor = atoi(l + 4) & 7;
		else if (!strncmp(l, "camera=", 7)) R.camMode = CLAMP(atoi(l + 7), 0, CAM_COUNT - 1);
	}
	fclose(f);
}

static void saveBest(void) {
	if (!sysHasStorage()) return;
	char p[96];
	sysDataPath(p, sizeof(p), "corrida.ini");
	FILE* f = fopen(p, "w");
	if (!f) return;
	fprintf(f, "melhor=%d\nvoltas=%d\ndificuldade=%d\ncor=%d\ncamera=%d\n", (int)(s_bestEver * 1000), R.laps, R.difficulty, R.playerColor, R.camMode);
	fclose(f);
}

static void toTitle(void) {
	R.state = RS_TITLE;
	R.stateT = 0;
	rgReset();
	s_hudState = -1;
	gfxInvalidate(GFX_BOTH);
}

static void startRace(void) {
	saveBest();  // guarda as opcoes escolhidas
	rgReset();
	R.state = RS_COUNTDOWN;
	R.countdown = 3.99f;
	R.stateT = 0;
	R.camH = rgPlayer()->h;
	R.fov = 60;
	s_newRecord = false;
	raBeep(880, 8, 230);
	s_hudState = -1;
	gfxInvalidate(GFX_BOTH);
}

// ---------------------------------------------------------------------------
static void raceEnter(void) {
	R.laps = 3;
	R.difficulty = 1;
	R.playerColor = 0;
	R.camMode = CAM_CHASE;
	loadBest();
	rwInit();
	raInit();
	toTitle();
	s_menuSel = 0;
	s_lastTick = tickGetCount();
	rwRender();
}

static void raceLeave(void) {
	raStop();
	saveBest();
	rwShutdown();
	free(s_panelBg);
	s_panelBg = NULL;
}

static void menuChange(int sel, int d) {
	switch (sel) {
		case 1: R.laps = CLAMP(R.laps + d, 1, 9); break;
		case 2: R.difficulty = (R.difficulty + d + 3) % 3; break;
		case 3:
			R.playerColor = (R.playerColor + d + 8) & 7;
			rgReset();
			break;
		case 4: R.camMode = (R.camMode + d + CAM_COUNT) % CAM_COUNT; break;
	}
	sndMove();
	gfxInvalidate(GFX_TOP);
}

#define MENU_Y 26
#define MENU_H 24

static void titleInput(void) {
	int old = s_menuSel;
	if (g_in.rep & KEY_DOWN) s_menuSel = (s_menuSel + 1) % 5;
	if (g_in.rep & KEY_UP) s_menuSel = (s_menuSel + 4) % 5;
	if (s_menuSel != old) {
		sndMove();
		gfxInvalidate(GFX_TOP);
	}
	if (g_in.rep & KEY_LEFT) menuChange(s_menuSel, -1);
	if (g_in.rep & KEY_RIGHT) menuChange(s_menuSel, +1);
	if (g_in.down & (KEY_A | KEY_START)) {
		if (s_menuSel == 0 || (g_in.down & KEY_START)) {
			sndClick();
			startRace();
		} else {
			menuChange(s_menuSel, +1);
		}
	}
	if (g_in.tap) {
		int y = g_in.sy;
		if (inRect(g_in.sx, y, 16, MENU_Y, SCR_W - 32, 28)) {
			sndClick();
			startRace();
		}
		for (int k = 1; k < 5; k++) {
			int ry = MENU_Y + 32 + (k - 1) * MENU_H;
			if (inRect(g_in.sx, y, 0, ry, SCR_W, MENU_H)) {
				s_menuSel = k;
				menuChange(k, g_in.sx < 150 ? -1 : +1);
			}
		}
	}
	if (g_in.down & KEY_B) {
		sndBack();
		appHome();
	}
}

static void pauseInput(void) {
	int old = s_pauseSel;
	if (g_in.rep & KEY_DOWN) s_pauseSel = (s_pauseSel + 1) % 4;
	if (g_in.rep & KEY_UP) s_pauseSel = (s_pauseSel + 3) % 4;
	if (old != s_pauseSel) {
		sndMove();
		gfxInvalidate(GFX_TOP);
	}
	int act = -1;
	if (g_in.down & KEY_A) act = s_pauseSel;
	if (g_in.down & (KEY_START | KEY_B)) act = 0;
	if (g_in.tap) {
		for (int k = 0; k < 4; k++)
			if (inRect(g_in.sx, g_in.sy, 40, 44 + k * 32, SCR_W - 80, 28)) act = k;
	}
	if (act == 0) {
		R.state = R.pausedFrom;
		s_lastTick = tickGetCount();
		gfxInvalidate(GFX_BOTH);
	} else if (act == 1) {
		startRace();
	} else if (act == 2) {
		R.camMode = (R.camMode + 1) % CAM_COUNT;
		gfxInvalidate(GFX_TOP);
	} else if (act == 3) {
		sndBack();
		toTitle();
	}
	if (act >= 0) sndClick();
}

static void raceFrame(void) {
	u64 now = tickGetCount();
	float dt = (float)(now - s_lastTick) / TICK_FREQ;
	s_lastTick = now;
	if (dt > 0.05f) dt = 0.05f;
	if (dt < 0.001f) dt = 0.001f;

	switch (R.state) {
		case RS_TITLE: titleInput(); break;
		case RS_PAUSE: pauseInput(); break;
		case RS_COUNTDOWN:
		case RS_RACE:
			if (g_in.down & KEY_START) {
				R.pausedFrom = R.state;
				R.state = RS_PAUSE;
				s_pauseSel = 0;
				sndClick();
				gfxInvalidate(GFX_BOTH);
			}
			if (g_in.down & KEY_X) {
				R.camMode = (R.camMode + 1) % CAM_COUNT;
				sndMove();
			}
			break;
		case RS_FINISH:
			if (R.stateT > 1.5f) {
				if (g_in.down & KEY_A || tapIn(16, 132, 108, 30)) {
					sndClick();
					startRace();
				} else if (g_in.down & KEY_B || tapIn(132, 132, 108, 30)) {
					sndBack();
					toTitle();
				}
			}
			break;
	}
	int prevState = R.state;
	rgUpdate(R.state == RS_PAUSE ? 0 : dt, g_in.held, g_in.down);
	// recorde de volta
	Car* p = rgPlayer();
	if (p->best > 0 && !R.autoUsed && (s_bestEver <= 0 || p->best < s_bestEver - 0.001f)) {
		s_bestEver = p->best;
		s_newRecord = true;
	}
	if (prevState != R.state) {
		gfxInvalidate(GFX_BOTH);
		if (R.state == RS_FINISH) saveBest();  // grava o recorde logo na chegada
	}
	raUpdate();
	rwRender();
	// HUD (tela de cima) a cada frame (so o que mudou e copiado); painel de toque:
	// corrida a 20 quadros/s, menus a 15 (borda de selecao pulsando) ou quando ha entrada
	gfxInvalidate(GFX_BOT);
	bool input = g_in.down || g_in.tDown || g_in.tUp;
	if (R.state == RS_COUNTDOWN || R.state == RS_RACE) {
		if ((g_frame % 3) == 0) gfxInvalidate(GFX_TOP);
	} else if (input || (g_frame % (R.state == RS_FINISH ? 15 : 4)) == 0) {
		gfxInvalidate(GFX_TOP);
	}
}

// ---------------------------------------------------------------------------
// HUD sobre o 3D (canvas g_bot)
// ---------------------------------------------------------------------------
static const u16 PILL = COL8(18, 22, 28);

static void pill(Canvas* c, int x, int y, int w, int h) { cvRRect(c, x, y, w, h, 8, PILL); }

static void outlined(Canvas* c, int f, int cx, int y, u16 col, const char* s) {
	int w = textWidth(f, s);
	int x = cx - w / 2;
	u16 o = COL8(12, 14, 20);
	for (int dy = -2; dy <= 2; dy += 2)
		for (int dx = -2; dx <= 2; dx += 2)
			if (dx || dy) cvText(c, f, x + dx, y + dy, o, s);
	cvText(c, f, x, y, col, s);
}

static void raceDrawHud(Canvas* c) {
	// redesenho inteiro so quando o "tipo" de HUD muda
	bool full = false;
	if (s_hudState != R.state) {
		cvClear(c, 0);
		s_hudState = R.state;
		memset(&s_hud, 0xFF, sizeof(s_hud));
		if (R.state == RS_TITLE) {
			outlined(c, FONT_BIG, SCR_W / 2, 10, COL8(255, 255, 255), "DSi Dash");
			outlined(c, FONT_H1, SCR_W / 2, 40, COL8(0, 210, 245), "RACING");
			cvRRect(c, 60, 160, 136, 22, 11, PILL);
			cvTextC(c, FONT_SMALL, SCR_W / 2, 163, COL8(255, 255, 255), "Circuito Macei\xC3\xB3");
			return;  // tela inteira sera copiada neste frame
		}
		full = true;
	}
	if (!full) gfxPresentRows(GFX_BOT, 0, 0);  // por padrao nao copia nada
	if (R.state == RS_TITLE) return;
	Car* p = rgPlayer();
	char b[48];
	bool top = false;
	// cada elemento so e redesenhado (e copiado) quando muda
	if (s_hud.place != p->place) {
		s_hud.place = p->place;
		cvFill(c, 6, 6, 62, 36, 0);
		pill(c, 6, 6, 62, 36);
		snprintf(b, sizeof(b), "%d\xC2\xBA", p->place);
		u16 pc = p->place == 1 ? COL8(255, 214, 64) : COL8(255, 255, 255);
		int ex = cvText(c, FONT_BIG, 14, 6, pc, b);
		snprintf(b, sizeof(b), "/%d", R.nCars);
		cvText(c, FONT_SMALL, ex + 2, 22, COL8(170, 180, 190), b);
		top = true;
	}
	int lapShown = MIN(p->lap + 1, R.laps);
	float tv = R.state == RS_FINISH ? p->finishTime : R.t;
	int cs = (int)(tv * 100 + 0.5f);
	if (s_hud.lap != lapShown || s_hud.cs != cs) {
		s_hud.lap = lapShown;
		s_hud.cs = cs;
		cvFill(c, 150, 6, 100, 36, 0);
		pill(c, 150, 6, 100, 36);
		snprintf(b, sizeof(b), "VOLTA %d/%d", lapShown, R.laps);
		cvText(c, FONT_SMALL, 160, 6, COL8(0, 210, 245), b);
		fmtTime(b, sizeof(b), tv);
		cvText(c, FONT_TITLE, 160, 19, COL8(255, 255, 255), b);
		top = true;
	}
	if (top && !full) gfxPresentRows(GFX_BOT, 6, 42);
	// velocimetro
	int kmh = (int)(fabsf(p->speed) * 3.6f), boost = p->boost > 0;
	if (s_hud.kmh != kmh || s_hud.boost != boost) {
		s_hud.kmh = kmh;
		s_hud.boost = boost;
		cvFill(c, 172, 150, 78, 36, 0);
		pill(c, 172, 150, 78, 36);
		snprintf(b, sizeof(b), "%d", kmh);
		cvTextR(c, FONT_BIG, 222, 152, boost ? COL8(255, 170, 40) : COL8(255, 255, 255), b);
		cvText(c, FONT_SMALL, 225, 166, COL8(170, 180, 190), "km/h");
		if (!full) gfxPresentRows(GFX_BOT, 150, 186);
	}
	// mensagem central
	int cd = R.state == RS_COUNTDOWN ? CLAMP((int)ceilf(R.countdown), 1, 3) : 0;
	const char* msg = cd ? "" : (R.msgT > 0 ? R.msg : "");
	if (s_hud.cd != cd || strcmp(s_hud.msg, msg)) {
		s_hud.cd = cd;
		snprintf(s_hud.msg, sizeof(s_hud.msg), "%s", msg);
		cvFill(c, 0, 60, SCR_W, 80, 0);
		if (cd) {
			snprintf(b, sizeof(b), "%d", cd);
			outlined(c, FONT_HUGE, SCR_W / 2, 66, COL8(255, 255, 255), b);
		} else if (msg[0]) {
			u16 mc = !strcmp(msg, "VAI!") ? COL8(80, 255, 120) : COL8(255, 230, 90);
			outlined(c, FONT_H1, SCR_W / 2, 84, mc, msg);
		}
		if (!full) gfxPresentRows(GFX_BOT, 60, 140);
	}
}

// ---------------------------------------------------------------------------
// tela de toque (canvas g_top)
// ---------------------------------------------------------------------------
static const u16 BG = COL8(28, 31, 38), CARD = COL8(42, 46, 56), TXT = COL8(240, 242, 246), TXT2 = COL8(150, 158, 170);

// minimapa: escala calculada uma vez
static float s_mmX0, s_mmZ0, s_mmSc;
static int s_mmOx, s_mmOy;

static void minimapInit(int x0, int y0, int size) {
	float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
	for (int i = 0; i < RT_N; i++) {
		float x, z;
		rgSecPos(i, &x, &z);
		minX = MIN(minX, x), maxX = MAX(maxX, x), minZ = MIN(minZ, z), maxZ = MAX(maxZ, z);
	}
	s_mmSc = (size - 16) / MAX(maxX - minX, maxZ - minZ);
	s_mmX0 = minX;
	s_mmZ0 = minZ;
	s_mmOx = x0 + (int)((size - (maxX - minX) * s_mmSc) / 2);
	s_mmOy = y0 + (int)((size - (maxZ - minZ) * s_mmSc) / 2);
}

static inline int mmX(float x) { return s_mmOx + (int)((x - s_mmX0) * s_mmSc); }
static inline int mmY(float z) { return s_mmOy + (int)((z - s_mmZ0) * s_mmSc); }

// velocimetro circular: 25 pontos
#define GAUGE_N 24
static s16 s_gx[GAUGE_N + 1], s_gy[GAUGE_N + 1];
static u16 s_gc[GAUGE_N + 1];

// fundo fixo do painel da corrida (montado uma vez)
static int s_panelBgFor = -1;

static void panelBgBuild(void) {
	if (!s_panelBg) s_panelBg = (u16*)malloc(SCR_W * SCR_H * 2);
	if (!s_panelBg) return;
	Canvas bc = g_top;
	bc.px = s_panelBg;
	cvResetClip(&bc);
	Canvas* c = &bc;
	cvClear(c, BG);
	// minimapa: tracado da pista
	minimapInit(6, 6, 122);
	cvRRect(c, 6, 6, 122, 122, 12, CARD);
	for (int i = 0; i < RT_N; i += 2) {
		float x, z;
		rgSecPos(i, &x, &z);
		cvCircle(c, mmX(x), mmY(z), 3, COL8(110, 118, 132));
	}
	float sx, sz;
	rgSecPos(RT_START_SEG, &sx, &sz);
	cvFill(c, mmX(sx) - 4, mmY(sz), 9, 2, TXT);
	// velocimetro apagado
	for (int k = 0; k <= GAUGE_N; k++) {
		float a = 3.14159f * (0.85f + 1.3f * k / GAUGE_N);  // de baixo-esquerda a baixo-direita
		s_gx[k] = 194 + (int)(cosf(a) * 30);
		s_gy[k] = 128 + (int)(sinf(a) * 30);
		s_gc[k] = lerpColor(COL8(0, 200, 240), COL8(255, 90, 90), k * 256 / GAUGE_N);
		cvCircle(c, s_gx[k], s_gy[k], 3, COL8(70, 76, 88));
	}
	cvTextC(c, FONT_SMALL, 194, 136, TXT2, "km/h");
	char b[16];
	snprintf(b, sizeof(b), "de %d", R.nCars);
	cvText(c, FONT_SMALL, 136 + 40, 12, TXT2, b);
	cvRRect(c, 8, 138, 118, 12, 6, CARD);
	cvHLine(c, 8, 166, SCR_W - 16, COL8(60, 64, 76));
	cvText(c, FONT_SMALL, 8, 172, TXT2, "A acelera  B freia  R drift  X c\xC3\xA2mera  START pausa");
	s_panelBgFor = R.nCars;
}

// Durante a corrida o painel e desenhado direto na VRAM da tela de baixo: so as areas que
// mudam sao restauradas a partir do fundo pronto e redesenhadas (nada de copiar 96 KB).
static Canvas s_vc;
static int s_panelLast = -1;
static s16 s_dotX[NCARS], s_dotY[NCARS];
static struct {
	int place, lap, lapCs, best, rec, kmh, boost, bar, drift;
} s_pv;

static void bgRestore(int x, int y, int w, int h) {
	// so escritas de 32 bits (a VRAM nao aceita escrita de 8 bits)
	if (x & 1) x--, w++;
	if (w & 1) w++;
	if (x < 0) w += x, x = 0;
	if (y < 0) h += y, y = 0;
	if (x + w > SCR_W) w = SCR_W - x;
	if (y + h > SCR_H) h = SCR_H - y;
	for (int j = 0; j < h; j++) {
		u32* d = (u32*)(s_vc.px + (y + j) * SCR_W + x);
		const u32* s = (const u32*)(s_panelBg + (y + j) * SCR_W + x);
		for (int i = 0; i < w / 2; i++) d[i] = s[i];
	}
}

static void panelRace(Canvas* cmem) {
	Car* p = rgPlayer();
	if (!s_panelBg || s_panelBgFor != R.nCars) {
		panelBgBuild();
		s_panelLast = -1;
	}
	if (!s_panelBg) {
		cvClear(cmem, BG);
		return;
	}
	gfxPresentRows(GFX_TOP, 0, 0);  // nada a copiar do buffer em memoria
	s_vc = *cmem;
	s_vc.px = (u16*)BG_GFX_SUB;
	cvResetClip(&s_vc);
	Canvas* c = &s_vc;
	if (s_panelLast != R.state) {
		s_panelLast = R.state;
		DC_FlushRange(s_panelBg, SCR_W * SCR_H * 2);
		dmaCopyWords(3, s_panelBg, BG_GFX_SUB, SCR_W * SCR_H * 2);
		memset(&s_pv, 0xFF, sizeof(s_pv));
		for (int k = 0; k < NCARS; k++) s_dotX[k] = -100;
	}
	// carros no minimapa
	for (int k = 0; k < R.nCars; k++)
		if (s_dotX[k] > -50) bgRestore(s_dotX[k] - 6, s_dotY[k] - 6, 12, 12);
	for (int k = R.nCars - 1; k >= 0; k--) {
		Car* car = &R.car[k];
		int px = mmX(car->x), py = mmY(car->z);
		if (k == 0) cvCircle(c, px, py, 5, TXT);
		cvCircle(c, px, py, 3, car->color | 0x8000);
		s_dotX[k] = px;
		s_dotY[k] = py;
	}
	char b[48], t[16];
	const int x = 136;
	if (s_pv.place != p->place) {
		s_pv.place = p->place;
		bgRestore(x - 2, 0, 40, 30);
		snprintf(b, sizeof(b), "%d\xC2\xBA", p->place);
		cvText(c, FONT_H1, x, 4, p->place == 1 ? COL8(255, 214, 64) : TXT, b);
	}
	int lap = MIN(p->lap + 1, R.laps);
	if (s_pv.lap != lap) {
		s_pv.lap = lap;
		bgRestore(x - 2, 30, SCR_W - x + 2, 22);
		snprintf(b, sizeof(b), "Volta %d/%d", lap, R.laps);
		cvText(c, FONT_BODY, x, 32, TXT, b);
	}
	float lt = R.state == RS_FINISH ? p->finishTime : R.t - p->lapStart;
	int lapCs = (int)(lt * 100 + 0.5f);
	if (s_pv.lapCs != lapCs) {
		s_pv.lapCs = lapCs;
		bgRestore(x - 2, 52, SCR_W - x + 2, 14);
		fmtTime(t, sizeof(t), lt);
		snprintf(b, sizeof(b), "Volta  %s", t);
		cvText(c, FONT_SMALL, x, 54, TXT2, b);
	}
	int best = (int)(p->best * 100 + 0.5f);
	if (s_pv.best != best) {
		s_pv.best = best;
		bgRestore(x - 2, 66, SCR_W - x + 2, 14);
		fmtTime(t, sizeof(t), p->best);
		snprintf(b, sizeof(b), "Melhor %s", t);
		cvText(c, FONT_SMALL, x, 68, TXT2, b);
	}
	int rec = (int)(s_bestEver * 100 + 0.5f) * 2 + s_newRecord;
	if (s_pv.rec != rec) {
		s_pv.rec = rec;
		bgRestore(x - 2, 80, SCR_W - x + 2, 14);
		fmtTime(t, sizeof(t), s_bestEver);
		snprintf(b, sizeof(b), "Recorde %s", t);
		cvText(c, FONT_SMALL, x, 82, s_newRecord ? COL8(255, 214, 64) : TXT2, b);
	}
	// velocimetro
	int kmh = (int)(fabsf(p->speed) * 3.6f), boost = p->boost > 0;
	if (s_pv.kmh != kmh || s_pv.boost != boost) {
		s_pv.kmh = kmh;
		s_pv.boost = boost;
		bgRestore(160, 94, 70, 56);
		int on = (int)(MIN(1.0f, kmh / 200.0f) * GAUGE_N + 0.5f);
		for (int k = 0; k < on && k <= GAUGE_N; k++) cvCircle(c, s_gx[k], s_gy[k], 3, boost ? COL8(255, 150, 40) : s_gc[k]);
		cvTextC(c, FONT_SMALL, 194, 136, TXT2, "km/h");
		snprintf(b, sizeof(b), "%d", kmh);
		cvTextC(c, FONT_TITLE, 194, 116, TXT, b);
	}
	// carga do drift / turbo
	const int by = 138, bw = 118;
	float fr = 0;
	u16 col = COL8(0, 200, 240);
	if (p->boost > 0) {
		fr = MIN(1.0f, p->boost / 1.5f);
		col = COL8(255, 150, 40);
	} else if (p->drifting) {
		fr = MIN(1.0f, p->charge / 2.3f);
		col = p->charge >= 2.3f ? COL8(255, 150, 40) : (p->charge >= 1.1f ? COL8(40, 160, 255) : COL8(150, 158, 170));
	}
	int bar = fr > 0 ? MAX(12, (int)(bw * fr)) : 0;
	if (s_pv.bar != bar || s_pv.drift != (int)col + p->drifting * 65536) {
		s_pv.bar = bar;
		s_pv.drift = (int)col + p->drifting * 65536;
		bgRestore(6, by, 154, 26);  // ate antes do velocimetro
		if (bar) cvRRect(c, 8, by, bar, 12, 6, col);
		cvText(c, FONT_SMALL, 10, by + 12, TXT2, p->drifting ? "Drift! Solte R para o turbo" : "Segure R numa curva: drift");
	}
}

static void panelTitle(Canvas* c) {
	cvTextC(c, FONT_TITLE, SCR_W / 2, 4, TXT, "Corrida");
	// botao principal
	bool sel0 = s_menuSel == 0;
	cvRRect(c, 16, MENU_Y, SCR_W - 32, 28, 14, COL8(0, 180, 220));
	cvTextC(c, FONT_TITLE, SCR_W / 2, MENU_Y + 3, COL8(255, 255, 255), "Correr!");
	if (sel0) cvRRectBorder(c, 12, MENU_Y - 4, SCR_W - 24, 36, 18, 3, uiSelColor());
	static const char* LAB[4] = {"Voltas", "Dificuldade", "Cor do carro", "C\xC3\xA2mera"};
	for (int k = 1; k < 5; k++) {
		int y = MENU_Y + 32 + (k - 1) * MENU_H;
		bool sel = s_menuSel == k;
		cvRRect(c, 10, y + 2, SCR_W - 20, MENU_H - 4, 8, sel ? COL8(56, 62, 76) : CARD);
		cvText(c, FONT_BODY, 20, y + 2, TXT, LAB[k - 1]);
		char v[32];
		switch (k) {
			case 1: snprintf(v, sizeof(v), "%d", R.laps); break;
			case 2: snprintf(v, sizeof(v), "%s", DIFF[R.difficulty]); break;
			case 3: v[0] = 0; break;
			case 4: snprintf(v, sizeof(v), "%s", CAMS[R.camMode]); break;
		}
		int vx = 190;
		cvIcon(c, IC_BACK_14, 132, y + 5, TXT2);
		cvIcon(c, IC_FWD_14, 232, y + 5, TXT2);
		if (k == 3) {
			cvRRect(c, vx - 22, y + 5, 44, 14, 7, rgPlayer()->color | 0x8000);
		} else {
			cvTextC(c, FONT_BODY, vx, y + 2, sel ? COL8(0, 210, 245) : TXT, v);
		}
		if (sel) cvRRectBorder(c, 8, y, SCR_W - 16, MENU_H, 10, 2, uiSelColor());
	}
	char t[16], b[48];
	fmtTime(t, sizeof(t), s_bestEver);
	snprintf(b, sizeof(b), "Recorde de volta: %s", t);
	cvTextC(c, FONT_SMALL, SCR_W / 2, 150, TXT2, b);
	cvHLine(c, 8, 170, SCR_W - 16, COL8(60, 64, 76));
	cvText(c, FONT_SMALL, 8, 175, TXT2, "A correr    Setas: escolher e mudar    B sair");
}

static void panelPause(Canvas* c) {
	cvTextC(c, FONT_TITLE, SCR_W / 2, 10, TXT, "Pausa");
	static const char* OPT[4] = {"Continuar", "Reiniciar corrida", "Trocar c\xC3\xA2mera", "Sair para o menu"};
	for (int k = 0; k < 4; k++) {
		int y = 44 + k * 32;
		bool sel = s_pauseSel == k;
		cvRRect(c, 40, y, SCR_W - 80, 28, 14, k == 0 ? COL8(0, 180, 220) : CARD);
		char b[48];
		if (k == 2) snprintf(b, sizeof(b), "C\xC3\xA2mera: %s", CAMS[R.camMode]);
		else snprintf(b, sizeof(b), "%s", OPT[k]);
		cvTextC(c, FONT_BODY, SCR_W / 2, y + 4, TXT, b);
		if (sel) cvRRectBorder(c, 36, y - 4, SCR_W - 72, 36, 18, 3, uiSelColor());
	}
}

static void panelResults(Canvas* c) {
	cvTextC(c, FONT_TITLE, SCR_W / 2, 2, TXT, "Resultado");
	int order[NCARS];
	for (int k = 0; k < R.nCars; k++) order[k] = k;
	for (int a = 0; a < R.nCars; a++)
		for (int b = a + 1; b < R.nCars; b++)
			if (R.car[order[b]].place < R.car[order[a]].place) {
				int t = order[a];
				order[a] = order[b];
				order[b] = t;
			}
	for (int k = 0; k < R.nCars; k++) {
		Car* car = &R.car[order[k]];
		int y = 26 + k * 17;
		if (order[k] == 0) cvRRect(c, 8, y - 1, SCR_W - 16, 17, 6, COL8(52, 60, 76));
		char b[24], t[16];
		snprintf(b, sizeof(b), "%d\xC2\xBA", car->place);
		cvText(c, FONT_BODY, 14, y - 2, car->place == 1 ? COL8(255, 214, 64) : TXT, b);
		cvCircle(c, 50, y + 7, 5, car->color | 0x8000);
		cvText(c, FONT_BODY, 62, y - 2, TXT, NAMES[order[k]]);
		if (car->finished) fmtTime(t, sizeof(t), car->finishTime);
		else snprintf(t, sizeof(t), "correndo\xE2\x80\xA6");
		cvTextR(c, FONT_BODY, SCR_W - 16, y - 2, car->finished ? TXT : TXT2, t);
	}
	if (R.stateT > 1.5f) {
		cvRRect(c, 16, 132, 108, 30, 15, COL8(0, 180, 220));
		cvTextC(c, FONT_BODY, 70, 137, COL8(255, 255, 255), "Correr de novo");
		cvRRect(c, 132, 132, 108, 30, 15, CARD);
		cvTextC(c, FONT_BODY, 186, 137, TXT, "Menu");
	}
	char t[16], b[64];
	fmtTime(t, sizeof(t), rgPlayer()->best);
	snprintf(b, sizeof(b), "Sua melhor volta: %s%s", t, s_newRecord ? "  \xE2\x98\x85 recorde!" : (R.autoUsed ? "  (piloto autom\xC3\xA1tico)" : ""));
	cvText(c, FONT_SMALL, 10, 168, s_newRecord ? COL8(255, 214, 64) : TXT2, b);
}

static void raceDrawTouch(Canvas* c) {
	uiResetAreas();
	if (R.state == RS_COUNTDOWN || R.state == RS_RACE) {
		panelRace(c);
	} else {
		s_panelLast = -1;
		cvClear(c, BG);
		switch (R.state) {
			case RS_TITLE: panelTitle(c); break;
			case RS_PAUSE: panelPause(c); break;
			case RS_FINISH: panelResults(c); break;
		}
	}
}

// drawTop = tela de BAIXO (sub engine); drawBot = HUD na tela de CIMA (ver cabecalho)
const App app_race = {"Corrida", raceEnter, raceLeave, raceFrame, raceDrawTouch, raceDrawHud};

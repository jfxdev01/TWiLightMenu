// DSi Dash — tela inicial (estilo HOME do Switch)
#include "common.h"

#define APP_GAMES_SPECIAL (-2)

typedef struct Tile {
	int app;
	const char* name;
	const char* desc;
	int icon;
	u32 c0, c1;
} Tile;

static const Tile TILES[] = {
	{APP_RACE, "Corrida", "DSi Dash Racing: corrida 3D no Circuito Maceió", IC_RACE_40, 0xFF8A3D, 0xE0322D},
	{APP_WEATHER, "Clima", "Previs\xC3\xA3o do tempo hora a hora e para 7 dias", IC_WEATHER_40, 0x56CCF2, 0x2F80ED},
	{APP_BROWSER, "Navegador", "Navegue na web em modo leitura", IC_BROWSER_40, 0x2BC0B4, 0x0B8793},
	{APP_NEWS, "Not\xC3\xAD" "cias", "Manchetes do dia via RSS", IC_NEWS_40, 0xFF6B6B, 0xD7263D},
	{APP_WIKI, "Wikip\xC3\xA9" "dia", "Pesquise e leia artigos", IC_WIKI_40, 0x9AA5B8, 0x4A5568},
	{APP_MAPS, "Mapas", "Mapa do mundo (OpenStreetMap) e busca de lugares", IC_MAP_40, 0x7ED56F, 0x2E9C63},
	{APP_CAMERA, "C\xC3\xA2mera", "Tire fotos e leia QR codes", IC_CAMERA_40, 0x9B6BFF, 0x5B2BD1},
	{APP_ALBUM, "\xC3\x81lbum", "Veja as fotos salvas no cart\xC3\xA3o SD", IC_ALBUM_40, 0x5FD38D, 0x1E9E5A},
	{APP_TRANSFER, "Transferir", "Envie arquivos do PC ou celular pelo Wi-Fi", IC_TRANSFER_40, 0xFFB347, 0xF0772B},
	{APP_FILES, "Arquivos", "Explore o cart\xC3\xA3o SD e abra apps .nds", IC_FILES_40, 0xF7B733, 0xD68910},
	{APP_NOTES, "Notas", "Bloco de notas salvo no cart\xC3\xA3o SD", IC_NOTES_40, 0xB5A48B, 0x7D6B55},
	{APP_TRANSLATE, "Tradutor", "Traduza textos entre vários idiomas", IC_TRANSLATE_40, 0x4DB6F7, 0x3949AB},
	{APP_CALC, "Calculadora", "Contas r\xC3\xA1pidas", IC_CALC_40, 0x7F8C9A, 0x46525E},
	{APP_GAMES_SPECIAL, "Jogos", "Abrir o TWiLight Menu++ (jogos DS/DSi/GBA)", IC_GAMES_40, 0xFF5FA2, 0xC2185B},
};
#define NTILES ARRAY_SIZE(TILES)

enum { BTN_WIFI, BTN_THEME, BTN_SETTINGS, BTN_POWER, NBTNS };
static const struct {
	const char* name;
	int icon;
	u32 color;
} BTNS[NBTNS] = {
	{"Wi-Fi", IC_WIFI_20, 0x00B4DC},
	{"Tema claro/escuro", IC_THEME_20, 0x8A6BE0},
	{"Configura\xC3\xA7\xC3\xB5" "es", IC_SETTINGS_20, 0x7A8793},
	{"Energia", IC_POWER_20, 0xE5484D},
};

#define TILE_Y 30
#define TILE_S 80
#define TILE_STRIDE 92
#define TILE_X0 18
#define BTN_Y 140
#define BTN_R 16
#define BTN_DX 44

static int s_sel, s_row, s_btn;
static int s_scroll;       // 8.8
static int s_target;       // pixels
static bool s_dragging;
static int s_grab, s_vel;
static int s_pressAnim;

static int tileX(int i) { return TILE_X0 + i * TILE_STRIDE; }
static int maxScroll(void) { return MAX(0, tileX(NTILES - 1) + TILE_S + TILE_X0 - SCR_W); }
static int btnX(int i) { return SCR_W / 2 - (NBTNS - 1) * BTN_DX / 2 + i * BTN_DX; }

static void ensureVisible(void) {
	int x = tileX(s_sel);
	int sc = s_target;
	if (x - sc < TILE_X0) sc = x - TILE_X0;
	if (x + TILE_S - sc > SCR_W - TILE_X0) sc = x + TILE_S - (SCR_W - TILE_X0);
	s_target = CLAMP(sc, 0, maxScroll());
}

static void powerChoice(int c) {
	if (c == 0) {
		if (sysHasLoader()) sysRequestExit();
		else if (!sysRebootTo("sd:/BOOT.NDS")) uiToast("Abra pelo TWiLight Menu++ para usar esta op\xC3\xA7\xC3\xA3o");
	} else if (c == 1) {
		sysSaveSettings();
		sysShutdown();
	}
}

static void openGames(void) {
	sndClick();
	if (sysHasLoader()) sysRequestExit();
	else if (!sysRebootTo("sd:/BOOT.NDS")) uiToast("TWiLight Menu++ n\xC3\xA3o encontrado");
}

static void activateTile(int i) {
	s_pressAnim = 8;
	if (TILES[i].app == APP_GAMES_SPECIAL) openGames();
	else appOpen(TILES[i].app);
}

static void activateBtn(int i) {
	switch (i) {
		case BTN_WIFI: appOpen(APP_WIFI); break;
		case BTN_THEME:
			sndClick();
			g_set.dark = !g_set.dark;
			themeApply(g_set.dark);
			sysSaveSettings();
			break;
		case BTN_SETTINGS: appOpen(APP_SETTINGS); break;
		case BTN_POWER: {
			static const char* opts[] = {"Voltar ao TWiLight Menu++", "Desligar", "Cancelar"};
			sndClick();
			uiDialog("Energia", NULL, opts, 3, powerChoice);
			break;
		}
	}
}

static void homeEnter(void) {
	s_target = CLAMP(s_target, 0, maxScroll());
	s_scroll = s_target << 8;
	gfxInvalidate(GFX_BOTH);
}

static void homeFrame(void) {
	int oldSel = s_sel, oldRow = s_row, oldBtn = s_btn;
	if (s_row == 0) {
		if (g_in.rep & KEY_RIGHT) s_sel = MIN(NTILES - 1, s_sel + 1);
		if (g_in.rep & KEY_LEFT) s_sel = MAX(0, s_sel - 1);
		if (g_in.rep & KEY_R) s_sel = MIN(NTILES - 1, s_sel + 3);
		if (g_in.rep & KEY_L) s_sel = MAX(0, s_sel - 3);
		if (g_in.down & KEY_DOWN) {
			s_row = 1;
			// botao mais proximo da tela
			int cx = tileX(s_sel) + TILE_S / 2 - (s_scroll >> 8);
			int best = 0;
			for (int i = 1; i < NBTNS; i++)
				if (abs(btnX(i) - cx) < abs(btnX(best) - cx)) best = i;
			s_btn = best;
		}
		if (g_in.down & KEY_A) activateTile(s_sel);
	} else {
		if (g_in.rep & KEY_RIGHT) s_btn = MIN(NBTNS - 1, s_btn + 1);
		if (g_in.rep & KEY_LEFT) s_btn = MAX(0, s_btn - 1);
		if (g_in.down & KEY_UP) s_row = 0;
		if (g_in.down & KEY_A) activateBtn(s_btn);
	}
	if (g_in.down & KEY_START) activateBtn(BTN_POWER);
	if (g_in.down & KEY_X) {
		wxRefresh();
		uiToast("Atualizando clima\xE2\x80\xA6");
	}
	if (g_in.down & KEY_Y) activateBtn(BTN_THEME);

	// toque
	if (g_in.tDown && inRect(g_in.tx, g_in.ty, 0, TILE_Y - 6, SCR_W, TILE_S + 12)) {
		s_dragging = true;
		s_grab = s_scroll >> 8;
		s_vel = 0;
	}
	if (s_dragging && g_in.touch && g_in.drag) {
		s_target = CLAMP(s_grab - (g_in.tx - g_in.sx), 0, maxScroll());
		s_scroll = s_target << 8;
		s_vel = -g_in.dx * 256;
		gfxInvalidate(GFX_BOT);
	}
	if (s_dragging && !g_in.touch) {
		s_dragging = false;
		if (g_in.tap) {
			int lx = g_in.sx + (s_scroll >> 8) - TILE_X0;
			int i = lx / TILE_STRIDE;
			if (lx >= 0 && i < NTILES && lx - i * TILE_STRIDE < TILE_S) {
				s_row = 0;
				s_sel = i;
				activateTile(i);
			}
		}
	}
	if (!s_dragging && s_vel) {
		s_target = CLAMP(s_target + s_vel / 256, 0, maxScroll());
		s_vel = s_vel * 7 / 8;
		if (abs(s_vel) < 128) s_vel = 0;
	}
	if (g_in.tap) {
		for (int i = 0; i < NBTNS; i++) {
			if (inRect(g_in.sx, g_in.sy, btnX(i) - BTN_R - 4, BTN_Y - BTN_R - 4, 2 * BTN_R + 8, 2 * BTN_R + 8)) {
				s_row = 1;
				s_btn = i;
				activateBtn(i);
			}
		}
	}

	if (s_sel != oldSel || s_row != oldRow || s_btn != oldBtn) {
		sndMove();
		if (s_row == 0) ensureVisible();
		gfxInvalidate(GFX_BOTH);
	}
	// rolagem suave
	int tgt = s_target << 8;
	if (s_scroll != tgt) {
		int d = (tgt - s_scroll) / 4;
		if (d == 0) d = tgt > s_scroll ? 1 : -1;
		s_scroll += d;
		if (abs(tgt - s_scroll) < 64) s_scroll = tgt;
		gfxInvalidate(GFX_BOT);
	}
	if (s_pressAnim > 0) {
		s_pressAnim--;
		gfxInvalidate(GFX_BOT);
	}
	gfxInvalidate(GFX_BOT);  // borda de selecao animada
	if (wxBusy() && (g_frame & 3) == 0) gfxInvalidate(GFX_TOP);
}

static void drawTile(Canvas* c, const Tile* t, int x, int y, int s, bool sel) {
	cvShadow(c, x, y, s, s, 14, T.dark ? 14 : 7);
	cvRRectGrad(c, x, y, s, s, 14, HEX(t->c0), HEX(t->c1));
	// brilho sutil no topo
	cvRRectA(c, x + 3, y + 3, s - 6, s / 2 - 4, 11, HEX(0xFFFFFF), 3);
	int is = icW(t->icon);
	cvIcon(c, t->icon, x + (s - is) / 2, y + (s - is) / 2, HEX(0xFFFFFF));
	if (sel) uiSelBorder(c, x, y, s, s, 14);
}

static void homeDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	int sc = s_scroll >> 8;
	// titulo do item focado
	const char* title = s_row == 0 ? TILES[s_sel].name : BTNS[s_btn].name;
	int tw = textWidth(FONT_TITLE, title);
	int tcx = s_row == 0 ? tileX(s_sel) + TILE_S / 2 - sc : btnX(s_btn);
	int tx = CLAMP(tcx - tw / 2, 8, SCR_W - 8 - tw);
	if (s_row == 0) cvText(c, FONT_TITLE, tx, 3, T.accent, title);

	for (int i = 0; i < NTILES; i++) {
		int x = tileX(i) - sc;
		if (x > SCR_W || x + TILE_S < 0) continue;
		int s = TILE_S, y = TILE_Y;
		if (i == s_sel && s_pressAnim > 0) {  // "aperta" ao abrir
			int k = s_pressAnim > 4 ? 8 - s_pressAnim : s_pressAnim;
			x += k;
			y += k;
			s -= 2 * k;
		}
		drawTile(c, &TILES[i], x, y, s, s_row == 0 && i == s_sel);
	}
	// indicador de pagina (pontos)
	int np = NTILES;
	int dx0 = SCR_W / 2 - (np - 1) * 4;
	for (int i = 0; i < np; i++) cvCircleA(c, dx0 + i * 8, TILE_Y + TILE_S + 10, 2, i == s_sel ? T.accent : T.text2, i == s_sel ? 32 : 12);

	// botoes redondos
	for (int i = 0; i < NBTNS; i++) {
		int bx = btnX(i);
		cvShadow(c, bx - BTN_R, BTN_Y - BTN_R, 2 * BTN_R, 2 * BTN_R, BTN_R, T.dark ? 12 : 6);
		cvCircle(c, bx, BTN_Y, BTN_R, T.surface);
		cvIcon(c, BTNS[i].icon, bx - 10, BTN_Y - 10, HEX(BTNS[i].color));
		if (s_row == 1 && i == s_btn) cvRing(c, bx, BTN_Y, BTN_R + 5, 3, uiSelColor());
	}
	if (s_row == 1) cvTextC(c, FONT_SMALL, btnX(s_btn), BTN_Y + BTN_R + 6, T.accent, BTNS[s_btn].name);

	static const Hint h[] = {{KEY_START, "Energia"}, {KEY_A, "Abrir"}};
	uiHints(c, h, 2);
}

static void drawWeatherCard(Canvas* c, int x, int y, int w, int h) {
	cvShadow(c, x, y, w, h, 12, T.dark ? 14 : 6);
	cvRRect(c, x, y, w, h, 12, T.surface);
	if (g_wx.valid) {
		wxDrawIcon(c, g_wx.code, g_wx.isDay, x + 8, y + (h - 24) / 2, false, !T.dark);
		char tb[16];
		fmtTemp(tb, sizeof(tb), g_wx.temp10);
		int ex = cvText(c, FONT_TITLE, x + 40, y + 3, T.text, tb);
		cvTextFit(c, FONT_BODY, ex + 8, y + 5, x + w - ex - 16, T.text, wmoText(g_wx.code));
		char line[96], mx[12], mn[12];
		fmtTemp(mx, sizeof(mx), g_wx.dMax10[0]);
		fmtTemp(mn, sizeof(mn), g_wx.dMin10[0]);
		if (g_wx.dCount > 0) snprintf(line, sizeof(line), "%s  \xE2\x80\xA2  M\xC3\xA1x %s  M\xC3\xADn %s", g_wx.city, mx, mn);
		else snprintf(line, sizeof(line), "%s", g_wx.city);
		cvTextFit(c, FONT_SMALL, x + 40, y + 25, w - 50, T.text2, line);
		if (wxBusy()) uiSpinner(c, x + w - 14, y + 12, 6, T.text2);
	} else if (wxBusy()) {
		uiSpinner(c, x + 22, y + h / 2, 9, T.accent);
		cvText(c, FONT_BODY, x + 42, y + (h - fontHeight(FONT_BODY)) / 2, T.text2, "Buscando clima\xE2\x80\xA6");
	} else {
		cvIcon(c, IC_WEATHER_20, x + 12, y + (h - 20) / 2, T.text2);
		cvTextFit(c, FONT_BODY, x + 42, y + 5, w - 50, T.text, "Clima indispon\xC3\xADvel");
		cvTextFit(c, FONT_SMALL, x + 42, y + 25, w - 50, T.text2, wxError()[0] ? wxError() : netStateText());
	}
}

static void homeDrawTop(Canvas* c) {
	cvGrad(c, 0, 0, SCR_W, SCR_H, T.dark ? HEX(0x333333) : HEX(0xF3F3F3), T.bg);
	uiStatusBar(c, false);
	struct tm t;
	sysLocalTm(&t);
	char tb[16];
	if (g_set.clock24) snprintf(tb, sizeof(tb), "%02d:%02d", t.tm_hour, t.tm_min);
	else snprintf(tb, sizeof(tb), "%d:%02d", (t.tm_hour % 12) ? t.tm_hour % 12 : 12, t.tm_min);
	cvTextC(c, FONT_HUGE, SCR_W / 2, 26, T.text, tb);
	if (!g_set.clock24) cvText(c, FONT_BODY, SCR_W / 2 + textWidth(FONT_HUGE, tb) / 2 + 3, 68, T.text2, t.tm_hour < 12 ? "AM" : "PM");
	char db[64];
	sysFormatDate(db, sizeof(db), &t);
	cvTextC(c, FONT_BODY, SCR_W / 2, 90, T.text2, db);
	drawWeatherCard(c, 14, 116, SCR_W - 28, 44);
	const char* desc = s_row == 0 ? TILES[s_sel].desc : "";
	if (textWidth(FONT_SMALL, desc) > SCR_W - 16) cvTextFit(c, FONT_SMALL, 8, 172, SCR_W - 16, T.text2, desc);
	else cvTextC(c, FONT_SMALL, SCR_W / 2, 172, T.text2, desc);
}

const App app_home = {"In\xC3\xAD" "cio", homeEnter, NULL, homeFrame, homeDrawTop, homeDrawBot};

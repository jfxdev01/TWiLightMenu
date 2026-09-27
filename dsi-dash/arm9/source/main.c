// DSi Dash v2 — launcher Wi-Fi para Nintendo DSi
// Tela de baixo (toque): interacao. Tela de cima: informacao.
// Toolchain: devkitARM + libnds 2 (calico) + dswifi 2
#include "common.h"

const App* g_apps[APP_COUNT];

static int s_cur = APP_HOME;
static int s_next = -1;
static int s_fadeStep;     // 0..FADE
static int s_fadeDir;      // +1 saindo, -1 entrando, 0 parado
#define FADE 6

void appOpen(int id) {
	if (id < 0 || id >= APP_COUNT || !g_apps[id]) {
		uiToast("Em breve!");
		return;
	}
	if (s_fadeDir || id == s_cur) return;
	if (id != APP_HOME) sndClick();
	s_next = id;
	s_fadeDir = 1;
	s_fadeStep = 0;
}

void appHome(void) { appOpen(APP_HOME); }
int appCurrent(void) { return s_cur; }

static void setFade(void) {
	int lvl = s_fadeStep * 16 / FADE;
	gfxBrightness(T.dark ? -lvl : lvl);
}

static void registerApps(void) {
	g_apps[APP_HOME] = &app_home;
	g_apps[APP_WEATHER] = &app_weather;
	g_apps[APP_SETTINGS] = &app_settings;
	g_apps[APP_WIFI] = &app_wifi;
	g_apps[APP_BROWSER] = &app_browser;
	g_apps[APP_NEWS] = &app_news;
	g_apps[APP_WIKI] = &app_wiki;
	g_apps[APP_CAMERA] = &app_camera;
	g_apps[APP_ALBUM] = &app_album;
	g_apps[APP_FILES] = &app_files;
	g_apps[APP_TRANSFER] = &app_transfer;
	g_apps[APP_NOTES] = &app_notes;
	g_apps[APP_CALC] = &app_calc;
	g_apps[APP_MAPS] = &app_maps;
	g_apps[APP_TRANSLATE] = &app_translate;
	g_apps[APP_RACE] = &app_race;
}

int main(void) {
	defaultExceptionHandler();
	gfxInit();
	sysInit();
	themeApply(g_set.dark);
	s_fadeStep = FADE;
	s_fadeDir = -1;
	setFade();
	registerApps();
	netInit();
	wxInit();

	const App* app = g_apps[s_cur];
	if (app->enter) app->enter();

	while (pmMainLoop() && !sysExitRequested()) {
		threadWaitForVBlank();
		gfxPresent();
		inputUpdate();
		sysTick();
		netTick();
		wxTick();
		if (appCurrent() != APP_WIFI) {
			char err[96];
			int cr = netConnectResult(err, sizeof(err));
			if (cr == 0) {
				uiToast("Wi-Fi conectado!");
				wxRefresh();
			} else if (cr == -1) {
				uiToast(err[0] ? err : "Falha ao conectar");
			}
		}

		if (s_fadeDir > 0) {
			if (++s_fadeStep >= FADE) {
				if (app->leave) app->leave();
				s_cur = s_next;
				s_next = -1;
				app = g_apps[s_cur];
				if (app->enter) app->enter();
				s_fadeDir = -1;
				gfxInvalidate(GFX_BOTH);
			}
			setFade();
		} else if (s_fadeDir < 0) {
			if (--s_fadeStep <= 0) {
				s_fadeStep = 0;
				s_fadeDir = 0;
			}
			setFade();
		} else if (kbdActive()) {
			kbdFrame();
		} else if (uiDialogActive()) {
			uiDialogFrame();
		} else {
			app->frame();
		}

		// com a thread de rede ocupada, sobra CPU para ela: desenha 20 quadros/s
		if (netBusy() && (g_frame % 3) != 0 && !g_in.touch && !g_in.down) continue;
		int d = gfxTakeRedraw();
		if (d & GFX_TOP) {
			if (kbdActive()) kbdDrawTop(&g_top);
			else app->drawTop(&g_top);
			gfxMarkDrawn(GFX_TOP);
		}
		if (d & GFX_BOT) {
			uiResetAreas();
			if (kbdActive()) {
				kbdDrawBot(&g_bot);
			} else {
				app->drawBot(&g_bot);
				uiDialogDraw(&g_bot);
			}
			uiDrawToast(&g_bot);
			gfxMarkDrawn(GFX_BOT);
		}
	}
	// saindo: escurece e volta ao loader
	sysSaveSettings();
	for (int i = 0; i <= 16; i += 2) {
		gfxBrightness(T.dark ? -i : i);
		threadWaitForVBlank();
	}
	return 0;
}

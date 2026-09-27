// DSi Dash — Configuracoes
#include "common.h"
#include <netinet/in.h>
#include <arpa/inet.h>

enum { S_THEME, S_BRIGHT, S_CLOCK, S_NETCLOCK, S_SOUNDS, S_IMAGES, S_CITY, S_HOME, S_FEED, S_WIFI, S_STORAGE, S_ABOUT, S_COUNT };

static const char* NAMES[S_COUNT] = {
	"Tema",
	"Brilho da tela",
	"Formato da hora",
	"Acertar rel\xC3\xB3gio pela internet",
	"Sons da interface",
	"Imagens no navegador",
	"Cidade do clima",
	"P\xC3\xA1gina inicial do navegador",
	"Feed de not\xC3\xAD" "cias (RSS)",
	"Rede Wi-Fi",
	"Armazenamento",
	"Sobre o DSi Dash",
};

static const char* DESCS[S_COUNT] = {
	"Escolha entre o tema claro e o escuro. Tamb\xC3\xA9m d\xC3\xA1 para trocar pelo bot\xC3\xA3o redondo na tela inicial (ou Y).",
	"Ajusta a luz de fundo das telas (5 n\xC3\xADveis). S\xC3\xB3 funciona no modo DSi.",
	"Mostrar a hora no formato 24 horas ou 12 horas (AM/PM).",
	"Usa a hora dos servidores da internet e o fuso da sua cidade para corrigir o rel\xC3\xB3gio, mesmo que o rel\xC3\xB3gio do console esteja errado.",
	"Toca sons curtos ao navegar e abrir apps.",
	"Baixa e mostra as fotos das pÃ¡ginas. Desligue para as pÃ¡ginas abrirem mais rÃ¡pido e gastarem menos dados.",
	"Por padr\xC3\xA3o a cidade \xC3\xA9 descoberta pelo seu IP. Digite um nome para escolher outra (deixe vazio para voltar ao autom\xC3\xA1tico).",
	"Endere\xC3\xA7o aberto quando o navegador inicia.",
	"Endere\xC3\xA7o do feed RSS usado pelo app Not\xC3\xAD" "cias.",
	"Veja o estado da conex\xC3\xA3o, procure redes e conecte com senha.",
	"Onde o DSi Dash salva configura\xC3\xA7\xC3\xB5" "es, fotos e notas.",
	"Vers\xC3\xA3o, modo de execu\xC3\xA7\xC3\xA3o e cr\xC3\xA9" "ditos.",
};

static ListView s_lv;
static NetJob s_geo;
static char s_geoQuery[64];
static char s_geoCity[64], s_geoLat[16], s_geoLon[16];

static void value(int i, char* out, int sz) {
	switch (i) {
		case S_THEME: snprintf(out, sz, "%s", g_set.dark ? "Escuro" : "Claro"); break;
		case S_BRIGHT: {
			int b = sysGetBacklight();
			if (b < 0) snprintf(out, sz, "Indispon\xC3\xADvel");
			else snprintf(out, sz, "%d de 5", b + 1);
			break;
		}
		case S_CLOCK: snprintf(out, sz, "%s", g_set.clock24 ? "24 horas" : "12 horas"); break;
		case S_NETCLOCK: snprintf(out, sz, "%s", g_set.netClock ? (sysClockSynced() ? "Ligado (sincronizado)" : "Ligado") : "Desligado"); break;
		case S_SOUNDS: snprintf(out, sz, "%s", g_set.sounds ? "Ligado" : "Desligado"); break;
		case S_IMAGES: snprintf(out, sz, "%s", g_set.images ? "Ligado" : "Desligado"); break;
		case S_CITY:
			if (jobBusy(&s_geo)) snprintf(out, sz, "Procurando\xE2\x80\xA6");
			else if (g_set.manualLoc) snprintf(out, sz, "%s", g_set.city);
			else snprintf(out, sz, "Autom\xC3\xA1tica (pelo IP)");
			break;
		case S_HOME: snprintf(out, sz, "%s", g_set.homepage); break;
		case S_FEED: snprintf(out, sz, "%s", g_set.newsFeed); break;
		case S_WIFI: snprintf(out, sz, "%s", netStateText()); break;
		case S_STORAGE: snprintf(out, sz, "%s", sysHasStorage() ? sysRoot() : "Nenhum cart\xC3\xA3o"); break;
		case S_ABOUT: snprintf(out, sz, "v" APP_VERSION); break;
		default: out[0] = 0;
	}
}

static void drawRow(Canvas* c, ListView* lv, int i, int x, int y, int w, int h, bool sel) {
	cvRRect(c, x + 6, y + 2, w - 12, h - 4, 8, sel ? T.surface2 : T.surface);
	cvTextFit(c, FONT_BODY, x + 14, y + (h - fontHeight(FONT_BODY)) / 2, 120, T.text, NAMES[i]);
	char v[96];
	value(i, v, sizeof(v));
	int vw = MIN(textWidth(FONT_SMALL, v), 96);
	cvTextFit(c, FONT_SMALL, x + w - 16 - vw, y + (h - fontHeight(FONT_SMALL)) / 2, 96, T.accent, v);
	if (sel) cvRRectBorder(c, x + 4, y, w - 8, h, 10, 2, uiSelColor());
}

static void geoJob(NetJob* j) {
	char q[128], url[256];
	urlEncode(s_geoQuery, q, sizeof(q));
	snprintf(url, sizeof(url), "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=pt&format=json", q);
	HttpResp r;
	if (httpGet(j, url, &r, 16384) < 0) {
		j->result = -1;
		return;
	}
	const char* res = strstr(r.body, "\"results\":[");
	double lat, lon;
	if (!res || !jsonStr(res, "name", s_geoCity, sizeof(s_geoCity)) || !jsonDouble(res, "latitude", &lat) || !jsonDouble(res, "longitude", &lon)) {
		snprintf(j->err, sizeof(j->err), "Cidade n\xC3\xA3o encontrada");
		httpFree(&r);
		j->result = -2;
		return;
	}
	char adm[48];
	if (jsonStr(res, "admin1", adm, sizeof(adm))) {
		int n = strlen(s_geoCity);
		snprintf(s_geoCity + n, sizeof(s_geoCity) - n, ", %s", adm);
	}
	snprintf(s_geoLat, sizeof(s_geoLat), "%.3f", lat);
	snprintf(s_geoLon, sizeof(s_geoLon), "%.3f", lon);
	httpFree(&r);
	j->result = 0;
}

static void onCity(const char* text) {
	if (!text) return;
	if (!text[0]) {
		g_set.manualLoc = false;
		sysSaveSettings();
		wxRefresh();
		uiToast("Cidade autom\xC3\xA1tica (pelo IP)");
		return;
	}
	snprintf(s_geoQuery, sizeof(s_geoQuery), "%s", text);
	s_geo.run = geoJob;
	netSubmit(&s_geo);
}

static void onHome(const char* text) {
	if (!text || !text[0]) return;
	snprintf(g_set.homepage, sizeof(g_set.homepage), "%s%s", strstr(text, "://") ? "" : "http://", text);
	sysSaveSettings();
}

static void onFeed(const char* text) {
	if (!text || !text[0]) return;
	snprintf(g_set.newsFeed, sizeof(g_set.newsFeed), "%s%s", strstr(text, "://") ? "" : "http://", text);
	sysSaveSettings();
}

static void change(int i, int dir) {
	switch (i) {
		case S_THEME:
			g_set.dark = !g_set.dark;
			themeApply(g_set.dark);
			break;
		case S_BRIGHT: {
			int b = sysGetBacklight();
			if (b < 0) {
				uiToast("Dispon\xC3\xADvel apenas no modo DSi");
				return;
			}
			b += dir;
			if (b > 4) b = 0;
			if (b < 0) b = 4;
			sysSetBacklight(b);
			break;
		}
		case S_CLOCK: g_set.clock24 = !g_set.clock24; break;
		case S_NETCLOCK: g_set.netClock = !g_set.netClock; break;
		case S_SOUNDS: g_set.sounds = !g_set.sounds; break;
		case S_IMAGES: g_set.images = !g_set.images; break;
		case S_CITY: kbdOpen("Cidade do clima (vazio = autom\xC3\xA1tico)", g_set.manualLoc ? g_set.city : "", 60, onCity); return;
		case S_HOME: kbdOpen("P\xC3\xA1gina inicial", g_set.homepage, 250, onHome); return;
		case S_FEED: kbdOpen("Endere\xC3\xA7o do feed RSS", g_set.newsFeed, 250, onFeed); return;
		case S_WIFI: appOpen(APP_WIFI); return;
		default: return;
	}
	sndClick();
	sysSaveSettings();
	gfxInvalidate(GFX_BOTH);
}

static void sEnter(void) {
	int sel = s_lv.sel;
	lvInit(&s_lv, 0, 28, SCR_W, HINTS_Y - 30, 26, S_COUNT, drawRow);
	s_lv.sel = sel;
	lvEnsureVisible(&s_lv);
}

static void sFrame(void) {
	if (s_geo.state == JOB_DONE) {
		s_geo.state = JOB_IDLE;
		if (s_geo.result == 0) {
			snprintf(g_set.city, sizeof(g_set.city), "%s", s_geoCity);
			snprintf(g_set.lat, sizeof(g_set.lat), "%s", s_geoLat);
			snprintf(g_set.lon, sizeof(g_set.lon), "%s", s_geoLon);
			g_set.manualLoc = true;
			sysSaveSettings();
			wxRefresh();
			char m[96];
			snprintf(m, sizeof(m), "Cidade: %s", s_geoCity);
			uiToast(m);
		} else {
			uiToast(s_geo.err[0] ? s_geo.err : "Falha ao procurar cidade");
		}
		gfxInvalidate(GFX_BOTH);
	}
	if (g_in.down & KEY_B) {
		sndBack();
		appHome();
		return;
	}
	int old = s_lv.sel;
	int act = lvUpdate(&s_lv);
	if (old != s_lv.sel) sndMove();
	if (act >= 0) change(act, +1);
	if (g_in.rep & KEY_RIGHT) change(s_lv.sel, +1);
	if (g_in.rep & KEY_LEFT) change(s_lv.sel, -1);
	gfxInvalidate(GFX_BOT);
}

static void aboutTop(Canvas* c) {
	int y = 60;
	char b[128];
	cvText(c, FONT_TITLE, 14, y, T.text, "DSi Dash v" APP_VERSION);
	y += 26;
	snprintf(b, sizeof(b), "Modo: %s  \xE2\x80\xA2  RAM: %d MB", sysIsDSi() ? "DSi" : "DS", sysRamMB());
	cvText(c, FONT_SMALL, 14, y, T.text2, b);
	y += 15;
	u32 ip = netIP();
	struct in_addr a = {ip};
	snprintf(b, sizeof(b), "Wi-Fi: %s%s%s", netStateText(), ip ? "  \xE2\x80\xA2  IP " : "", ip ? inet_ntoa(a) : "");
	cvText(c, FONT_SMALL, 14, y, T.text2, b);
	y += 15;
	snprintf(b, sizeof(b), "Cart\xC3\xA3o: %s", sysHasStorage() ? sysRoot() : "n\xC3\xA3o encontrado");
	cvText(c, FONT_SMALL, 14, y, T.text2, b);
	y += 20;
	cvTextWrap(c, FONT_SMALL, 14, y, SCR_W - 28, 4, T.text2,
		"Fonte Nunito (SIL OFL). Clima: open-meteo.com e ip-api.com. Feito com devkitARM, libnds e calico.");
}

static void sDrawTop(Canvas* c) {
	cvClear(c, T.bg);
	uiStatusBar(c, false);
	uiHeader(c, IC_SETTINGS_20, "Configura\xC3\xA7\xC3\xB5" "es");
	int i = s_lv.sel;
	if (i == S_ABOUT) {
		aboutTop(c);
		return;
	}
	cvText(c, FONT_TITLE, 14, 58, T.text, NAMES[i]);
	char v[256];
	value(i, v, sizeof(v));
	cvTextWrap(c, FONT_BODY, 14, 82, SCR_W - 28, 2, T.accent, v);
	cvTextWrap(c, FONT_SMALL, 14, 120, SCR_W - 28, 4, T.text2, DESCS[i]);
}

static void sDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	uiBackButton(c);
	cvTextC(c, FONT_TITLE, SCR_W / 2, 3, T.text, "Configura\xC3\xA7\xC3\xB5" "es");
	lvDraw(c, &s_lv);
	static const Hint h[] = {{KEY_A, "Alterar"}, {KEY_B, "Voltar"}};
	uiHints(c, h, 2);
}

const App app_settings = {"Configura\xC3\xA7\xC3\xB5" "es", sEnter, NULL, sFrame, sDrawTop, sDrawBot};

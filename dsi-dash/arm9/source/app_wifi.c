// DSi Dash — app Wi-Fi: estado da conexao, procurar redes, conectar com senha
#include "common.h"
#include <netinet/in.h>
#include <arpa/inet.h>

static ListView s_lv;
static WlanBssDesc* s_list;
static int s_count;       // -1 = procurando
static bool s_scanned;
static WlanBssDesc s_pick;
static bool s_connecting;

static void drawRow(Canvas* c, ListView* lv, int i, int x, int y, int w, int h, bool sel) {
	cvRRect(c, x + 6, y + 2, w - 12, h - 4, 8, sel ? T.surface2 : T.surface);
	if (i == 0) {
		cvIcon(c, IC_SEARCH_14, x + 14, y + (h - 14) / 2, T.accent);
		cvText(c, FONT_BODY, x + 34, y + (h - fontHeight(FONT_BODY)) / 2, T.text, s_count < 0 ? "Procurando redes\xE2\x80\xA6" : "Procurar redes");
		if (s_count < 0) uiSpinner(c, x + w - 24, y + h / 2, 6, T.accent);
	} else {
		const WlanBssDesc* b = &s_list[i - 1];
		char ssid[36];
		int n = MIN(b->ssid_len, 32);
		memcpy(ssid, b->ssid, n);
		ssid[n] = 0;
		bool cur = netState() == NET_ONLINE && !strcmp(ssid, netSsid());
		cvTextFit(c, FONT_BODY, x + 14, y + (h - fontHeight(FONT_BODY)) / 2, w - 90, cur ? T.accent : T.text, ssid);
		int bars = wlanCalcSignalStrength(b->rssi);
		int bx = x + w - 34;
		for (int k = 0; k < 4; k++) {
			int bh = 3 + k * 2;
			cvFillA(c, bx + k * 4, y + h / 2 + 5 - bh, 3, bh, T.text, k <= bars ? 32 : 8);
		}
		if (netBssSecure(b)) cvIcon(c, IC_LOCK_14, bx - 18, y + (h - 14) / 2, T.text2);
		if (cur) cvIcon(c, IC_CHECK_14, bx - 36, y + (h - 14) / 2, T.accent);
		if (netBssNeedsWpa(b) && !sysIsDSi()) cvText(c, FONT_SMALL, bx - 70, y + 5, T.danger, "s\xC3\xB3 DSi");
	}
	if (sel) cvRRectBorder(c, x + 4, y, w - 8, h, 10, 2, uiSelColor());
}

static void startScan(void) {
	if (netScanStart()) {
		s_count = -1;
		s_scanned = true;
	} else {
		uiToast("N\xC3\xA3o foi poss\xC3\xADvel procurar agora");
	}
	gfxInvalidate(GFX_BOTH);
}

static void onPass(const char* pass) {
	if (!pass) return;
	if (netConnectTo(&s_pick, pass)) {
		s_connecting = true;
		uiToast("Conectando\xE2\x80\xA6");
	}
}

static void wfEnter(void) {
	lvInit(&s_lv, 0, 28, SCR_W, HINTS_Y - 30, 26, 1, drawRow);
	s_count = 0;
	s_scanned = false;
	if (netState() != NET_ONLINE) startScan();  // conectado: so procura quando pedir (desconecta um instante)
}

static void wfFrame(void) {
	if (s_count < 0 || !s_scanned) {
		int n = netScanPoll(&s_list);
		if (n >= 0 && s_count < 0) {
			s_count = n;
			s_lv.count = 1 + n;
			gfxInvalidate(GFX_BOTH);
		}
	}
	if (s_connecting) {
		char err[96];
		int r = netConnectResult(err, sizeof(err));
		if (r == 0) {
			s_connecting = false;
			uiToast("Conectado!");
			wxRefresh();
		} else if (r == -1) {
			s_connecting = false;
			uiToast(err[0] ? err : "Falha ao conectar");
		}
		gfxInvalidate(GFX_BOTH);
	}
	if (g_in.down & KEY_B) {
		sndBack();
		appHome();
		return;
	}
	if (g_in.down & KEY_X) startScan();
	if (g_in.down & KEY_Y) {
		netReconnect();
		uiToast("Reconectando com o perfil do console\xE2\x80\xA6");
	}
	int old = s_lv.sel;
	int act = lvUpdate(&s_lv);
	if (old != s_lv.sel) sndMove();
	if (act == 0) startScan();
	else if (act > 0 && s_count > 0 && !s_connecting) {
		s_pick = s_list[act - 1];
		if (netBssNeedsWpa(&s_pick) && !sysIsDSi() && !(s_pick.auth_mask & 0x0F)) {
			uiToast("Redes WPA/WPA2 s\xC3\xB3 funcionam no modo DSi");
		} else if (netBssSecure(&s_pick)) {
			char t[64];
			snprintf(t, sizeof(t), "Senha de \"%.*s\"", MIN(s_pick.ssid_len, 32), s_pick.ssid);
			kbdOpen(t, "", 63, onPass);
		} else {
			onPass("");
		}
	}
	gfxInvalidate(GFX_BOT);
	if ((g_frame & 31) == 0) gfxInvalidate(GFX_TOP);
}

static void wfDrawTop(Canvas* c) {
	cvClear(c, T.bg);
	uiStatusBar(c, false);
	uiHeader(c, IC_WIFI_20, "Wi-Fi");
	cvShadow(c, 12, 58, SCR_W - 24, 112, 12, T.dark ? 14 : 6);
	cvRRect(c, 12, 58, SCR_W - 24, 112, 12, T.surface);
	NetState st = netState();
	u16 dot = st == NET_ONLINE ? T.ok : (st == NET_CONNECTING ? HEX(0xF5A623) : T.danger);
	cvCircle(c, 30, 74, 6, dot);
	cvText(c, FONT_TITLE, 42, 63, T.text, s_connecting ? "Conectando\xE2\x80\xA6" : netStateText());
	char b[96];
	int y = 88;
	if (st == NET_ONLINE) {
		snprintf(b, sizeof(b), "Rede: %s", netSsid());
		cvTextFit(c, FONT_BODY, 24, y, SCR_W - 48, T.text, b);
		y += 20;
		u32 gw, mask, d1, d2;
		netIPInfo(&gw, &mask, &d1, &d2);
		struct in_addr a;
		a.s_addr = netIP();
		snprintf(b, sizeof(b), "IP %s", inet_ntoa(a));
		cvText(c, FONT_SMALL, 24, y, T.text2, b);
		a.s_addr = gw;
		snprintf(b, sizeof(b), "Gateway %s", inet_ntoa(a));
		cvText(c, FONT_SMALL, 130, y, T.text2, b);
		y += 15;
		a.s_addr = d1;
		snprintf(b, sizeof(b), "DNS %s", inet_ntoa(a));
		cvText(c, FONT_SMALL, 24, y, T.text2, b);
		snprintf(b, sizeof(b), "Sinal %d/3", MAX(0, netBars()));
		cvText(c, FONT_SMALL, 130, y, T.text2, b);
	} else if (st == NET_NOCONFIG) {
		cvTextWrap(c, FONT_SMALL, 24, y, SCR_W - 48, 4, T.text2, "Nenhuma rede salva no console. Escolha uma rede na lista abaixo e digite a senha.");
	} else {
		cvTextWrap(c, FONT_SMALL, 24, y, SCR_W - 48, 4, T.text2, "Aguardando conex\xC3\xA3o. Voc\xC3\xAA pode escolher outra rede abaixo ou apertar Y para tentar de novo com o perfil do console.");
	}
	cvTextWrap(c, FONT_SMALL, 16, 174, SCR_W - 32, 1, T.text2, sysIsDSi() ? "Modo DSi: redes abertas, WEP, WPA e WPA2" : "Modo DS: s\xC3\xB3 redes abertas e WEP");
}

static void wfDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	uiBackButton(c);
	cvTextC(c, FONT_TITLE, SCR_W / 2, 3, T.text, "Redes pr\xC3\xB3ximas");
	lvDraw(c, &s_lv);
	if (s_scanned && s_count == 0) cvTextC(c, FONT_SMALL, SCR_W / 2, 70, T.text2, "Nenhuma rede encontrada");
	static const Hint h[] = {{KEY_X, "Procurar"}, {KEY_A, "Conectar"}, {KEY_B, "Voltar"}};
	uiHints(c, h, 3);
}

const App app_wifi = {"Wi-Fi", wfEnter, NULL, wfFrame, wfDrawTop, wfDrawBot};

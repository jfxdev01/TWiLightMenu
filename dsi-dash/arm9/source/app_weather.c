// DSi Dash — app Clima
#include "common.h"

static int s_view;  // 0 = 7 dias, 1 = grafico 24h

static void wEnter(void) {
	if (!g_wx.valid || (long)(sysNow() - g_wx.fetched) > 5 * 60) wxRefresh();
	gfxInvalidate(GFX_BOTH);
}

static void wFrame(void) {
	if (g_in.down & KEY_B) {
		sndBack();
		appHome();
		return;
	}
	if (g_in.down & KEY_A) wxRefresh();
	if (g_in.down & (KEY_L | KEY_R | KEY_LEFT | KEY_RIGHT)) {
		s_view ^= 1;
		sndMove();
		gfxInvalidate(GFX_BOT);
	}
	if (tapIn(40, 0, 176, 26)) {
		s_view ^= 1;
		sndMove();
		gfxInvalidate(GFX_BOT);
	}
	if (wxBusy() && (g_frame & 3) == 0) gfxInvalidate(GFX_BOTH);
}

static u16 tempColor(int t10) {
	int t = CLAMP(t10, 0, 380);
	if (t < 180) return lerpColor(HEX(0x4FC3F7), HEX(0x9CCC65), t * 256 / 180);
	return lerpColor(HEX(0xFFCA28), HEX(0xFF7043), (t - 180) * 256 / 200);
}

static void wDrawTop(Canvas* c) {
	if (!g_wx.valid) {
		cvClear(c, T.bg);
		uiStatusBar(c, false);
		if (wxBusy()) {
			uiSpinner(c, SCR_W / 2, 90, 14, T.accent);
			cvTextC(c, FONT_BODY, SCR_W / 2, 116, T.text2, "Buscando clima\xE2\x80\xA6");
		} else {
			uiEmpty(c, IC_WEATHER_40, "Clima indispon\xC3\xADvel", wxError()[0] ? wxError() : netStateText());
		}
		return;
	}
	const Weather* w = &g_wx;
	u16 top, bot;
	wxSkyColors(w->code, w->isDay, &top, &bot);
	cvGrad(c, 0, 0, SCR_W, SCR_H, top, bot);
	uiStatusBar(c, true);
	u16 white = HEX(0xFFFFFF);
	u16 soft = HEX(0xE3EEF7);

	char buf[96];
	snprintf(buf, sizeof(buf), "%s%s%s", w->city, w->region[0] ? ", " : "", w->region);
	cvTextFit(c, FONT_BODY, 12, 24, 170, white, buf);
	if (w->fetched) {
		struct tm ft;
		time_t f = w->fetched;
		gmtime_r(&f, &ft);
		snprintf(buf, sizeof(buf), "atualizado %02d:%02d", ft.tm_hour, ft.tm_min);
		cvTextR(c, FONT_SMALL, SCR_W - 10, 27, soft, buf);
	}
	wxDrawIcon(c, w->code, w->isDay, 12, 48, true, false);
	char tb[16];
	fmtTemp(tb, sizeof(tb), w->temp10);
	cvText(c, FONT_HUGE, 66, 34, white, tb);
	int ry = 42;
	char fl[16];
	fmtTemp(fl, sizeof(fl), w->feels10);
	snprintf(buf, sizeof(buf), "SensaÃ§Ã£o %s", fl);
	cvText(c, FONT_SMALL, 164, ry, white, buf);
	snprintf(buf, sizeof(buf), "Umidade %d%%", w->hum);
	cvText(c, FONT_SMALL, 164, ry + 13, white, buf);
	snprintf(buf, sizeof(buf), "Vento %d km/h", (w->wind10 + 5) / 10);
	cvText(c, FONT_SMALL, 164, ry + 26, white, buf);
	if (w->sunrise[0]) {
		snprintf(buf, sizeof(buf), "Sol %sâ%s", w->sunrise, w->sunset);
		cvText(c, FONT_SMALL, 164, ry + 39, white, buf);
	}
	cvTextFit(c, FONT_TITLE, 14, 96, SCR_W - 28, white, wmoText(w->code));

	// faixa horaria (a cada 3h)
	int y0 = 120;
	cvRRectA(c, 6, y0, SCR_W - 12, 70, 10, HEX(0xFFFFFF), 6);
	int n = 0;
	for (int i = 0; i < w->hCount && n < 8; i += 3, n++) {
		int cx = 6 + 15 + n * 30;
		snprintf(buf, sizeof(buf), i == 0 ? "Agora" : "%dh", w->hHour[i]);
		cvTextC(c, FONT_SMALL, cx, y0 + 1, soft, buf);
		wxDrawIcon(c, w->hCode[i], w->hDay[i], cx - 12, y0 + 17, false, false);
		fmtTemp(tb, sizeof(tb), w->hTemp10[i]);
		cvTextC(c, FONT_SMALL, cx, y0 + 38, white, tb);
		if (w->hPop[i] >= 20) {
			snprintf(buf, sizeof(buf), "%d%%", w->hPop[i]);
			cvTextC(c, FONT_SMALL, cx, y0 + 52, HEX(0xB3E5FC), buf);
		}
	}
	if (wxBusy()) uiSpinner(c, SCR_W - 16, 44, 6, white);
}

static void drawDays(Canvas* c) {
	const Weather* w = &g_wx;
	int gmin = 1000, gmax = -1000;
	for (int i = 0; i < w->dCount; i++) {
		gmin = MIN(gmin, w->dMin10[i]);
		gmax = MAX(gmax, w->dMax10[i]);
	}
	if (gmax <= gmin) gmax = gmin + 10;
	const int bx0 = 150, bx1 = 206;
	for (int i = 0; i < w->dCount; i++) {
		int y = 30 + i * 20;
		if (i & 1) cvFillA(c, 6, y - 1, SCR_W - 12, 20, T.text, 2);
		char lab[24];
		if (i == 0) snprintf(lab, sizeof(lab), "Hoje");
		else if (i == 1) snprintf(lab, sizeof(lab), "Amanh\xC3\xA3");
		else {
			int wd = (int)(((sysDaysFromCivil(w->dY[i], w->dM[i], w->dD[i]) % 7) + 11) % 7);
			snprintf(lab, sizeof(lab), "%s %02d", sysWeekdayShort(wd), w->dD[i]);
		}
		cvText(c, FONT_BODY, 12, y, T.text, lab);
		wxDrawIcon(c, w->dCode[i], true, 64, y - 3, false, !T.dark);
		if (w->dPop[i] >= 20) {
			snprintf(lab, sizeof(lab), "%d%%", w->dPop[i]);
			cvText(c, FONT_SMALL, 92, y + 2, HEX(0x29A3E0), lab);
		}
		char mn[12], mx[12];
		fmtTemp(mn, sizeof(mn), w->dMin10[i]);
		fmtTemp(mx, sizeof(mx), w->dMax10[i]);
		cvTextR(c, FONT_BODY, bx0 - 4, y, T.text2, mn);
		cvRRect(c, bx0, y + 7, bx1 - bx0, 5, 2, T.line);
		int a = bx0 + (w->dMin10[i] - gmin) * (bx1 - bx0) / (gmax - gmin);
		int b = bx0 + (w->dMax10[i] - gmin) * (bx1 - bx0) / (gmax - gmin);
		if (b - a < 5) b = a + 5;
		for (int x = a; x < b; x++) cvFill(c, x, y + 7, 1, 5, tempColor(w->dMin10[i] + (w->dMax10[i] - w->dMin10[i]) * (x - a) / MAX(1, b - a)));
		cvText(c, FONT_BODY, bx1 + 5, y, T.text, mx);
	}
}

static void drawGraph(Canvas* c) {
	const Weather* w = &g_wx;
	if (w->hCount < 2) return;
	int x0 = 26, x1 = SCR_W - 12, y0 = 40, y1 = 150;
	int mn = 1000, mx = -1000;
	for (int i = 0; i < w->hCount; i++) {
		mn = MIN(mn, w->hTemp10[i]);
		mx = MAX(mx, w->hTemp10[i]);
	}
	mn -= 10;
	mx += 10;
	// barras de chance de chuva
	for (int i = 0; i < w->hCount; i++) {
		int x = x0 + i * (x1 - x0) / (w->hCount - 1);
		int h = w->hPop[i] * (y1 - y0) / 100;
		if (h > 0) cvFillA(c, x - 3, y1 - h, 6, h, HEX(0x29A3E0), 10);
	}
	for (int g = 0; g <= 2; g++) {
		int y = y0 + g * (y1 - y0) / 2;
		cvHLine(c, x0, y, x1 - x0, T.line);
		char lb[12];
		fmtTemp(lb, sizeof(lb), mx - g * (mx - mn) / 2);
		cvTextR(c, FONT_SMALL, x0 - 3, y - 8, T.text2, lb);
	}
	int px = 0, py = 0;
	for (int i = 0; i < w->hCount; i++) {
		int x = x0 + i * (x1 - x0) / (w->hCount - 1);
		int y = y1 - (w->hTemp10[i] - mn) * (y1 - y0) / MAX(1, mx - mn);
		if (i) {
			u16 col = tempColor(w->hTemp10[i]);
			cvLine(c, px, py, x, y, col);
			cvLine(c, px, py + 1, x, y + 1, col);
		}
		if (i % 3 == 0) {
			cvCircle(c, x, y, 3, tempColor(w->hTemp10[i]));
			char lb[8];
			snprintf(lb, sizeof(lb), "%dh", w->hHour[i]);
			cvTextC(c, FONT_SMALL, x, y1 + 2, T.text2, lb);
		}
		px = x;
		py = y;
	}
	// ponto selecionado pelo toque
	if (g_in.touch && inRect(g_in.tx, g_in.ty, x0 - 8, y0 - 10, x1 - x0 + 16, y1 - y0 + 30)) {
		int i = CLAMP((g_in.tx - x0 + (x1 - x0) / (2 * (w->hCount - 1))) * (w->hCount - 1) / (x1 - x0), 0, w->hCount - 1);
		int x = x0 + i * (x1 - x0) / (w->hCount - 1);
		int y = y1 - (w->hTemp10[i] - mn) * (y1 - y0) / MAX(1, mx - mn);
		cvVLine(c, x, y0, y1 - y0, T.accent);
		cvCircle(c, x, y, 5, T.accent);
		char lb[48], tb[12];
		fmtTemp(tb, sizeof(tb), w->hTemp10[i]);
		snprintf(lb, sizeof(lb), "%dh: %s \xE2\x80\xA2 chuva %d%%", w->hHour[i], tb, w->hPop[i]);
		cvTextC(c, FONT_SMALL, SCR_W / 2, 26, T.accent, lb);
		gfxInvalidate(GFX_BOT);
	}
}

static void wDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	uiBackButton(c);
	const char* title = s_view == 0 ? "Pr\xC3\xB3ximos 7 dias" : "Pr\xC3\xB3ximas 24 horas";
	cvTextC(c, FONT_TITLE, SCR_W / 2, 3, T.text, title);
	cvText(c, FONT_SMALL, SCR_W - 34, 6, T.text2, s_view == 0 ? "1/2" : "2/2");
	if (!g_wx.valid) {
		if (wxBusy()) uiSpinner(c, SCR_W / 2, 96, 12, T.accent);
		else cvTextC(c, FONT_BODY, SCR_W / 2, 90, T.text2, "Sem dados. Aperte A para tentar.");
	} else if (s_view == 0) {
		drawDays(c);
	} else {
		drawGraph(c);
	}
	static const Hint h[] = {{KEY_R, "Trocar"}, {KEY_A, "Atualizar"}, {KEY_B, "Voltar"}};
	uiHints(c, h, 3);
}

const App app_weather = {"Clima", wEnter, NULL, wFrame, wDrawTop, wDrawBot};

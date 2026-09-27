// DSi Dash — Transferir: servidor de arquivos pelo Wi-Fi (abra o endereco no PC/celular)
#include "common.h"
#include "srv.h"
#include "qrcodegen.h"
#include <netinet/in.h>
#include <arpa/inet.h>

static u8 s_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
static bool s_qrOk;
static char s_url[64];
static int s_lastVer = -1;

static void makeUrl(void) {
	struct in_addr a;
	a.s_addr = netIP();
	if (srvPort() == 80 || !srvPort()) snprintf(s_url, sizeof(s_url), "http://%s/", inet_ntoa(a));
	else snprintf(s_url, sizeof(s_url), "http://%s:%d/", inet_ntoa(a), srvPort());
	u8 tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
	s_qrOk = qrcodegen_encodeText(s_url, tmp, s_qr, qrcodegen_Ecc_MEDIUM, 1, 4, qrcodegen_Mask_AUTO, true);
}

static void tEnter(void) {
	s_url[0] = 0;
	if (netState() == NET_ONLINE && sysHasStorage()) srvStart();
	gfxInvalidate(GFX_BOTH);
}

static void tLeave(void) {
	if (srvRunning()) srvStop();
}

static void tFrame(void) {
	if (g_in.down & KEY_B) {
		sndBack();
		appHome();
		return;
	}
	if (!srvRunning() && netState() == NET_ONLINE && sysHasStorage() && (g_frame % 60) == 0) srvStart();
	if (srvRunning() && !s_url[0] && srvPort()) {
		makeUrl();
		gfxInvalidate(GFX_BOTH);
	}
	const SrvStatus* st = srvStatus();
	if (st->version != s_lastVer || st->active) {
		s_lastVer = st->version;
		if ((g_frame % 6) == 0) gfxInvalidate(GFX_BOTH);
	}
}

static void tDrawTop(Canvas* c) {
	cvClear(c, T.bg);
	uiStatusBar(c, false);
	uiHeader(c, IC_TRANSFER_20, "Transferir arquivos");
	if (!sysHasStorage()) {
		uiEmpty(c, IC_TRANSFER_40, "Sem cart\xC3\xA3o SD", NULL);
		return;
	}
	if (netState() != NET_ONLINE) {
		uiEmpty(c, IC_WIFI_40, "Sem Wi-Fi", "Conecte a uma rede para transferir arquivos.");
		return;
	}
	if (!s_url[0]) {
		uiSpinner(c, SCR_W / 2, 100, 12, T.accent);
		return;
	}
	// QR code do endereco
	if (s_qrOk) {
		int n = qrcodegen_getSize(s_qr);
		int sc = MAX(1, 104 / (n + 8));
		int w = (n + 8) * sc;
		int x0 = 10, y0 = 58;
		cvRRect(c, x0 - 2, y0 - 2, w + 4, w + 4, 6, HEX(0xFFFFFF));
		for (int y = 0; y < n; y++)
			for (int x = 0; x < n; x++)
				if (qrcodegen_getModule(s_qr, x, y)) cvFill(c, x0 + (x + 4) * sc, y0 + (y + 4) * sc, sc, sc, HEX(0x000000));
	}
	int tx = 134;
	int y = 56;
	y += cvTextWrap(c, FONT_SMALL, tx, y, SCR_W - tx - 6, 3, T.text2, "No PC ou celular (mesma rede Wi-Fi), abra:");
	y += 4 + cvTextWrap(c, FONT_TITLE, tx, y + 4, SCR_W - tx - 4, 2, T.accent, s_url + 7);
	cvTextWrap(c, FONT_SMALL, tx, y + 6, SCR_W - tx - 6, 3, T.text2, "ou aponte a cÃ¢mera do celular para o QR code.");
}

static void tDrawBot(Canvas* c) {
	cvClear(c, T.bg);
	uiBackButton(c);
	cvTextC(c, FONT_TITLE, SCR_W / 2, 3, T.text, "Atividade");
	const SrvStatus* st = srvStatus();
	int y = 30;
	cvRRect(c, 8, y, SCR_W - 16, 58, 10, T.surface);
	if (st->active) {
		cvTextFit(c, FONT_BODY, 18, y + 6, SCR_W - 36, T.text, st->file);
		int pct = st->total > 0 ? (int)((long long)st->done * 100 / st->total) : 0;
		uiProgress(c, 18, y + 30, SCR_W - 36, pct);
		char b[64];
		snprintf(b, sizeof(b), "%s %ld KB de %ld KB (%d%%)", st->active == 1 ? "Recebendo" : "Enviando", st->done / 1024, st->total / 1024, pct);
		cvText(c, FONT_SMALL, 18, y + 38, T.text2, b);
	} else {
		cvText(c, FONT_BODY, 18, y + 6, T.text, srvRunning() ? "Aguardando conex\xC3\xA3o\xE2\x80\xA6" : "Servidor parado");
		char b[96];
		snprintf(b, sizeof(b), "%d recebido(s)  \xE2\x80\xA2  %d enviado(s)  \xE2\x80\xA2  %d pedido(s)", st->uploads, st->downloads, st->requests);
		cvText(c, FONT_SMALL, 18, y + 30, T.text2, b);
	}
	y += 66;
	for (int i = 0; i < st->logN && i < 4; i++) cvTextFit(c, FONT_SMALL, 14, y + i * 17, SCR_W - 28, i ? T.text2 : T.text, st->log[i]);
	static const Hint h[] = {{KEY_B, "Parar e voltar"}};
	uiHints(c, h, 1);
}

const App app_transfer = {"Transferir", tEnter, tLeave, tFrame, tDrawTop, tDrawBot};

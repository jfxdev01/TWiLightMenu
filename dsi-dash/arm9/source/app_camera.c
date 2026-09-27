// DSi Dash — Camera: previa ao vivo, fotos em JPEG no cartao SD e leitor de QR code
#include "common.h"
#include <malloc.h>
#include "camera.h"
#include "jpeg.h"
#include "quirc.h"
#include <dirent.h>
#include <sys/stat.h>

enum { CS_OFF, CS_STARTING, CS_PREVIEW, CS_ERROR };

static int s_state;
static int s_dev = CAM_OUTER;
static u16* s_buf[2];
static int s_cur;             // buffer sendo preenchido
static u16* s_shown;          // ultimo quadro completo
static bool s_qr;
static struct quirc* s_quirc;
static char s_qrText[512];
static int s_qrKind;          // 0 texto, 1 url, 2 wifi
static char s_qrSsid[40], s_qrPass[72];
static int s_flash;
static u16* s_thumb;          // miniatura da ultima foto (64x48)
static char s_lastPath[128];
static char s_msg[96];
static int s_frames;

// ---------------------------------------------------------------------------
static void freeBufs(void) {
	for (int i = 0; i < 2; i++) {
		free(s_buf[i]);
		s_buf[i] = NULL;
	}
	s_shown = NULL;
	if (s_quirc) {
		quirc_destroy(s_quirc);
		s_quirc = NULL;
	}
}

static void camEnter(void) {
	s_qrText[0] = 0;
	if (!sysIsDSi()) {
		s_state = CS_OFF;
		return;
	}
	s_state = CS_STARTING;
	s_frames = 0;
}

static void camLeave(void) {
	if (s_state == CS_PREVIEW || s_state == CS_STARTING) {
		while (camBusy()) swiDelay(1000);
		camDeinit();
	}
	freeBufs();
	s_state = CS_OFF;
	gfxInvalidate(GFX_BOTH);
}

static bool startCamera(void) {
	for (int i = 0; i < 2; i++) {
		if (!s_buf[i]) s_buf[i] = (u16*)memalign(32, CAM_PREVIEW_W * CAM_PREVIEW_H * 2);
		if (!s_buf[i]) {
			snprintf(s_msg, sizeof(s_msg), "Sem mem\xC3\xB3ria");
			return false;
		}
	}
	if (!camInit()) {
		snprintf(s_msg, sizeof(s_msg), "A c\xC3\xA2mera n\xC3\xA3o respondeu");
		return false;
	}
	if (!camSelect(s_dev)) {
		snprintf(s_msg, sizeof(s_msg), "Falha ao selecionar a c\xC3\xA2mera");
		return false;
	}
	s_cur = 0;
	camStartPreview(s_buf[0]);
	return true;
}

// ---------------------------------------------------------------------------
// fotos
// ---------------------------------------------------------------------------
static int nextPhotoNumber(const char* dir) {
	int hi = 0;
	DIR* d = opendir(dir);
	if (!d) return 1;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (!strncasecmp(e->d_name, "DSI_", 4)) hi = MAX(hi, atoi(e->d_name + 4));
	}
	closedir(d);
	return hi + 1;
}

static void makeThumb(const u16* src) {
	if (!s_thumb) s_thumb = (u16*)malloc(64 * 48 * 2);
	if (!s_thumb || !src) return;
	for (int y = 0; y < 48; y++)
		for (int x = 0; x < 64; x++) s_thumb[y * 64 + x] = src[(y * 4) * 256 + x * 4] | 0x8000;
}

static void takePhoto(void) {
	if (!sysHasStorage()) {
		uiToast("Sem cart\xC3\xA3o SD para salvar fotos");
		return;
	}
	u16* big = (u16*)memalign(32, CAM_PHOTO_W * CAM_PHOTO_H * 2);
	if (!big) {
		uiToast("Sem mem\xC3\xB3ria para a foto");
		return;
	}
	// som do obturador + flash branco
	sndClick();
	while (camBusy()) swiDelay(1000);
	camStop();
	makeThumb(s_shown);
	camStartPhoto(big);
	int guard = 0;
	while (camBusy() && guard++ < 300) threadWaitForVBlank();
	camStop();
	DC_InvalidateRange(big, CAM_PHOTO_W * CAM_PHOTO_H * 2);
	s_flash = 8;
	gfxBrightness(12);

	u8* jpg = NULL;
	int n = jpegEncodeYuv422((const u8*)big, CAM_PHOTO_W, CAM_PHOTO_H, 88, &jpg);
	free(big);
	if (n <= 0) {
		uiToast("Erro ao codificar a foto");
	} else {
		char dir[64];
		snprintf(dir, sizeof(dir), "%sDCIM", sysRoot());
		mkdir(dir, 0777);
		snprintf(dir, sizeof(dir), "%sDCIM/100DSIDA", sysRoot());
		mkdir(dir, 0777);
		snprintf(s_lastPath, sizeof(s_lastPath), "%s/DSI_%04d.JPG", dir, nextPhotoNumber(dir));
		FILE* f = fopen(s_lastPath, "wb");
		bool ok = f && fwrite(jpg, 1, n, f) == (size_t)n;
		if (f) fclose(f);
		free(jpg);
		char m[160];
		snprintf(m, sizeof(m), ok ? "Foto salva: %s" : "Erro ao salvar %s", strrchr(s_lastPath, '/') + 1);
		uiToast(m);
	}
	s_cur = 0;
	camStartPreview(s_buf[0]);
	gfxInvalidate(GFX_BOTH);
}

static void swapCamera(void) {
	while (camBusy()) swiDelay(1000);
	camStop();
	s_dev = s_dev == CAM_OUTER ? CAM_INNER : CAM_OUTER;
	if (!camSelect(s_dev)) uiToast("Falha ao trocar de c\xC3\xA2mera");
	s_cur = 0;
	camStartPreview(s_buf[0]);
	sndMove();
	gfxInvalidate(GFX_BOTH);
}

// ---------------------------------------------------------------------------
// QR
// ---------------------------------------------------------------------------
static void parseWifi(const char* s) {
	// WIFI:T:WPA;S:minha rede;P:senha;;
	s_qrSsid[0] = s_qrPass[0] = 0;
	const char* p = s + 5;
	while (*p) {
		char key = p[0];
		if (p[1] != ':') break;
		p += 2;
		char val[80];
		int n = 0;
		while (*p && *p != ';' && n < 79) {
			if (*p == '\\' && p[1]) p++;
			val[n++] = *p++;
		}
		val[n] = 0;
		if (*p == ';') p++;
		if (key == 'S') snprintf(s_qrSsid, sizeof(s_qrSsid), "%s", val);
		else if (key == 'P') snprintf(s_qrPass, sizeof(s_qrPass), "%s", val);
	}
}

static void scanQr(const u16* img) {
	if (!s_quirc) {
		s_quirc = quirc_new();
		if (!s_quirc || quirc_resize(s_quirc, CAM_PREVIEW_W, CAM_PREVIEW_H) < 0) return;
	}
	int w, h;
	u8* g = quirc_begin(s_quirc, &w, &h);
	for (int i = 0; i < w * h; i++) {
		u16 c = img[i];
		int r = c & 31, gg = (c >> 5) & 31, b = (c >> 10) & 31;
		g[i] = (r * 77 + gg * 150 + b * 29) >> 5;
	}
	quirc_end(s_quirc);
	int n = quirc_count(s_quirc);
	for (int i = 0; i < n; i++) {
		static struct quirc_code code;  // estaticos: grandes demais para a pilha
		static struct quirc_data data;
		quirc_extract(s_quirc, i, &code);
		if (quirc_decode(&code, &data) != QUIRC_SUCCESS) continue;
		int l = MIN(data.payload_len, (int)sizeof(s_qrText) - 1);
		if (!strncmp(s_qrText, (const char*)data.payload, l) && s_qrText[l] == 0) return;
		memcpy(s_qrText, data.payload, l);
		s_qrText[l] = 0;
		if (!strncasecmp(s_qrText, "http://", 7) || !strncasecmp(s_qrText, "https://", 8)) s_qrKind = 1;
		else if (!strncmp(s_qrText, "WIFI:", 5)) {
			s_qrKind = 2;
			parseWifi(s_qrText);
		} else s_qrKind = 0;
		sndClick();
		gfxInvalidate(GFX_BOTH);
		return;
	}
}

static void qrAction(void) {
	if (!s_qrText[0]) return;
	if (s_qrKind == 1) browserOpenUrl(s_qrText);
	else if (s_qrKind == 2) {
		if (netConnectSsid(s_qrSsid, s_qrPass)) uiToast("Conectando \xC3\xA0 rede do QR code\xE2\x80\xA6");
	} else if (sysHasStorage()) {
		char p[96];
		sysDataPath(p, sizeof(p), "notas.txt");
		FILE* f = fopen(p, "a");
		if (f) {
			fprintf(f, "%s\n\n", s_qrText);
			fclose(f);
			uiToast("Texto salvo em Notas");
		}
	}
}

// ---------------------------------------------------------------------------
#define SHUT_X 128
#define SHUT_Y 84
#define SIDE_R 20

static void camFrame(void) {
	if (s_flash > 0) {
		s_flash--;
		gfxBrightness(s_flash * 12 / 8);
	}
	if (g_in.down & KEY_B) {
		if (s_qr && s_qrText[0]) {
			s_qrText[0] = 0;
			gfxInvalidate(GFX_BOTH);
			return;
		}
		sndBack();
		appHome();
		return;
	}
	if (s_state == CS_STARTING) {
		if (++s_frames == 3) s_state = startCamera() ? CS_PREVIEW : CS_ERROR;
		gfxInvalidate(GFX_BOTH);
		return;
	}
	if (s_state != CS_PREVIEW) return;
	// quadro pronto?
	if (!camBusy()) {
		camStop();
		DC_InvalidateRange(s_buf[s_cur], CAM_PREVIEW_W * CAM_PREVIEW_H * 2);
		s_shown = s_buf[s_cur];
		s_cur ^= 1;
		camStartPreview(s_buf[s_cur]);
		gfxInvalidate(GFX_TOP);
		if (s_qr && (++s_frames % 4) == 0 && !s_qrText[0]) scanQr(s_shown);
	}
	if (g_in.down & (KEY_A | KEY_L | KEY_R)) {
		if (s_qr && s_qrText[0] && (g_in.down & KEY_A)) qrAction();
		else if (!s_qr) takePhoto();
	}
	if (g_in.down & KEY_X) swapCamera();
	if (g_in.down & KEY_Y) {
		s_qr = !s_qr;
		s_qrText[0] = 0;
		sndMove();
		gfxInvalidate(GFX_BOTH);
	}
	if (g_in.tap) {
		int x = g_in.sx, y = g_in.sy;
		if ((x - SHUT_X) * (x - SHUT_X) + (y - SHUT_Y) * (y - SHUT_Y) < 36 * 36) {
			if (s_qr && s_qrText[0]) qrAction();
			else if (!s_qr) takePhoto();
		} else if ((x - 48) * (x - 48) + (y - SHUT_Y) * (y - SHUT_Y) < 26 * 26) swapCamera();
		else if ((x - 208) * (x - 208) + (y - SHUT_Y) * (y - SHUT_Y) < 26 * 26) {
			s_qr = !s_qr;
			s_qrText[0] = 0;
			gfxInvalidate(GFX_BOTH);
		} else if (inRect(x, y, 8, 130, 70, 40)) appOpen(APP_ALBUM);
	}
	gfxInvalidate(GFX_BOT);
}

static void camDrawTop(Canvas* c) {
	if (s_state == CS_PREVIEW && s_shown) {
		for (int y = 0; y < SCR_H; y++) {
			const u16* src = s_shown + y * CAM_PREVIEW_W;
			u16* dst = c->px + y * SCR_W;
			for (int x = 0; x < SCR_W; x++) dst[x] = src[x] | 0x8000;
		}
		cvRRectA(c, 6, 6, 104, 18, 9, HEX(0x000000), 14);
		cvText(c, FONT_SMALL, 14, 7, HEX(0xFFFFFF), s_dev == CAM_OUTER ? "C\xC3\xA2mera externa" : "C\xC3\xA2mera interna");
		if (s_qr) {
			// moldura de mira
			u16 w = HEX(0xFFFFFF);
			int x0 = 68, y0 = 36, s = 120;
			for (int k = 0; k < 3; k++) {
				cvFill(c, x0, y0 + k, 24, 1, w), cvFill(c, x0 + k, y0, 1, 24, w);
				cvFill(c, x0 + s - 24, y0 + k, 24, 1, w), cvFill(c, x0 + s - 1 - k, y0, 1, 24, w);
				cvFill(c, x0, y0 + s - 1 - k, 24, 1, w), cvFill(c, x0 + k, y0 + s - 24, 1, 24, w);
				cvFill(c, x0 + s - 24, y0 + s - 1 - k, 24, 1, w), cvFill(c, x0 + s - 1 - k, y0 + s - 24, 1, 24, w);
			}
			cvRRectA(c, 40, 166, 176, 20, 10, HEX(0x000000), 16);
			cvTextC(c, FONT_SMALL, SCR_W / 2, 168, w, "Aponte para um QR code");
		}
		return;
	}
	cvClear(c, HEX(0x101010));
	uiStatusBar(c, true);
	if (s_state == CS_OFF) {
		cvIcon(c, IC_CAMERA_40, (SCR_W - 40) / 2, 56, HEX(0x9A9A9A));
		cvTextC(c, FONT_TITLE, SCR_W / 2, 104, HEX(0xFFFFFF), "C\xC3\xA2mera indispon\xC3\xADvel");
		cvTextWrap(c, FONT_SMALL, 24, 128, SCR_W - 48, 3, HEX(0xBBBBBB), "As c\xC3\xA2meras s\xC3\xB3 funcionam quando o DSi Dash roda no modo DSi (pelo Unlaunch ou TWiLight com o modo DSi ligado).");
	} else if (s_state == CS_ERROR) {
		cvTextC(c, FONT_TITLE, SCR_W / 2, 84, HEX(0xFFFFFF), s_msg);
	} else {
		uiSpinner(c, SCR_W / 2, 90, 12, HEX(0xFFFFFF));
		cvTextC(c, FONT_BODY, SCR_W / 2, 112, HEX(0xDDDDDD), "Ligando a c\xC3\xA2mera\xE2\x80\xA6");
	}
}

static void camDrawBot(Canvas* c) {
	u16 bg = HEX(0x1C1C1C), fg = HEX(0xFFFFFF), btn = HEX(0x333333);
	cvClear(c, bg);
	cvCircle(c, 16, 14, 11, btn);
	cvIcon(c, IC_BACK_14, 9, 7, fg);
	cvTextC(c, FONT_TITLE, SCR_W / 2, 3, fg, s_qr ? "Leitor de QR code" : "C\xC3\xA2mera");
	if (s_qr && s_qrText[0]) {
		cvRRect(c, 10, 28, SCR_W - 20, 132, 12, HEX(0x2A2A2A));
		const char* kind = s_qrKind == 1 ? "Link" : (s_qrKind == 2 ? "Rede Wi-Fi" : "Texto");
		cvText(c, FONT_SMALL, 20, 32, T.accent, kind);
		if (s_qrKind == 2) {
			char b[96];
			snprintf(b, sizeof(b), "Rede: %s", s_qrSsid);
			cvTextFit(c, FONT_BODY, 20, 50, SCR_W - 40, fg, b);
		} else {
			cvTextWrap(c, FONT_BODY, 20, 50, SCR_W - 40, 4, fg, s_qrText);
		}
		uiButton(c, 20, 124, SCR_W - 40, 28, s_qrKind == 1 ? "Abrir no navegador" : (s_qrKind == 2 ? "Conectar" : "Salvar em Notas"), true, false);
		static const Hint h[] = {{KEY_A, "Usar"}, {KEY_B, "Fechar"}};
		uiHints(c, h, 2);
		return;
	}
	// obturador
	cvCircle(c, SHUT_X, SHUT_Y, 34, fg);
	cvCircle(c, SHUT_X, SHUT_Y, 29, bg);
	cvCircle(c, SHUT_X, SHUT_Y, 26, s_qr ? T.accent : fg);
	if (s_qr) cvIcon(c, IC_QR_20, SHUT_X - 10, SHUT_Y - 10, fg);
	// trocar camera / QR
	cvCircle(c, 48, SHUT_Y, SIDE_R, btn);
	cvIcon(c, IC_SWAPCAM_20, 38, SHUT_Y - 10, fg);
	cvCircle(c, 208, SHUT_Y, SIDE_R, s_qr ? T.accent : btn);
	cvIcon(c, IC_QR_20, 198, SHUT_Y - 10, fg);
	// miniatura
	if (s_thumb) {
		cvRRect(c, 10, 128, 68, 52, 6, fg);
		cvImage(c, s_thumb, 64, 48, 12, 130);
	}
	static const Hint h[] = {{KEY_X, "Trocar"}, {KEY_Y, "QR"}, {KEY_A, "Foto"}, {KEY_B, "Voltar"}};
	uiHints(c, h, 4);
	cvFill(c, 0, HINTS_Y, SCR_W, 1, bg);
}

const App app_camera = {"C\xC3\xA2mera", camEnter, camLeave, camFrame, camDrawTop, camDrawBot};

// DSi Dash — Mapas (OpenStreetMap). O mapa ocupa as duas telas como uma so.
#include "common.h"
#include "image.h"
#include <math.h>

#define TILE 256
#define VIEW_W SCR_W
#define VIEW_H (SCR_H + HINTS_Y)   // tela de cima + area do mapa na de baixo
#define ZMIN 2
#define ZMAX 18

typedef struct Tile {
	int z, x, y;
	u16* px;
	u32 used;
	u8 state;  // 1 ok, 2 erro
} Tile;

static Tile s_tiles[16];
static int s_maxTiles;
static Mutex s_mtx;
static NetJob s_job;
static s64 s_cx, s_cy;   // centro em pixels do mundo (zoom atual)
static int s_z = 13;
static bool s_init;
static double s_homeLat, s_homeLon;
static bool s_haveHome;
static int s_vel[2];
static bool s_drag;
static s64 s_gcx, s_gcy;
static NetJob s_search;
static char s_query[96], s_found[160];
static double s_fLat, s_fLon;
static bool s_mark;
static double s_markLat, s_markLon;
static u32 s_tick;

static void lonlatToPx(double lat, double lon, int z, s64* px, s64* py) {
	double n = (double)(TILE << z);
	double lr = lat * M_PI / 180.0;
	*px = (s64)((lon + 180.0) / 360.0 * n);
	*py = (s64)((1.0 - log(tan(lr) + 1.0 / cos(lr)) / M_PI) / 2.0 * n);
}

static void pxToLonlat(s64 px, s64 py, int z, double* lat, double* lon) {
	double n = (double)(TILE << z);
	*lon = px / n * 360.0 - 180.0;
	double m = M_PI * (1.0 - 2.0 * py / n);
	*lat = atan(sinh(m)) * 180.0 / M_PI;
}

static Tile* findTile(int z, int x, int y) {
	for (int i = 0; i < s_maxTiles; i++)
		if (s_tiles[i].px && s_tiles[i].z == z && s_tiles[i].x == x && s_tiles[i].y == y) return &s_tiles[i];
	return NULL;
}

static bool failedTile(int z, int x, int y) {
	for (int i = 0; i < s_maxTiles; i++)
		if (s_tiles[i].state == 2 && s_tiles[i].z == z && s_tiles[i].x == x && s_tiles[i].y == y) return true;
	return false;
}

// proximo tile visivel que falta (mais perto do centro primeiro)
static bool nextMissing(int* oz, int* ox, int* oy) {
	int z = s_z;
	s64 left = s_cx - VIEW_W / 2, top = s_cy - VIEW_H / 2;
	int tx0 = (int)(left >> 8), ty0 = (int)(top >> 8);
	int tx1 = (int)((left + VIEW_W - 1) >> 8), ty1 = (int)((top + VIEW_H - 1) >> 8);
	int ctx = (int)(s_cx >> 8), cty = (int)(s_cy >> 8);
	int lim = 1 << z;
	int best = 1 << 30;
	bool found = false;
	for (int ty = ty0; ty <= ty1; ty++)
		for (int tx = tx0; tx <= tx1; tx++) {
			if (ty < 0 || ty >= lim) continue;
			int wx = ((tx % lim) + lim) % lim;
			if (findTile(z, wx, ty) || failedTile(z, wx, ty)) continue;
			int d = abs(tx - ctx) + abs(ty - cty);
			if (d < best) {
				best = d;
				*oz = z;
				*ox = wx;
				*oy = ty;
				found = true;
			}
		}
	return found;
}

static void storeTile(int z, int x, int y, u16* px, u8 state) {
	mutexLock(&s_mtx);
	// substitui o menos usado recentemente
	int slot = 0;
	u32 oldest = 0xFFFFFFFF;
	for (int i = 0; i < s_maxTiles; i++) {
		if (!s_tiles[i].px && !s_tiles[i].state) {
			slot = i;
			break;
		}
		if (s_tiles[i].used < oldest) {
			oldest = s_tiles[i].used;
			slot = i;
		}
	}
	free(s_tiles[slot].px);
	s_tiles[slot] = (Tile){z, x, y, px, s_tick, state};
	mutexUnlock(&s_mtx);
}

static void tileJob(NetJob* j) {
	int z, x, y;
	int n = 0;
	while (!j->cancel && n < 12 && nextMissing(&z, &x, &y)) {
		char url[128];
		snprintf(url, sizeof(url), "https://tile.openstreetmap.org/%d/%d/%d.png", z, x, y);
		HttpResp r;
		u16* px = NULL;
		HttpReq rq = {0};
		rq.maxBody = 200 * 1024;
		rq.extraHeaders = "Referer: https://www.openstreetmap.org/\r\n";
		if (httpRequest(j, url, &rq, &r) == 0) {
			int w, h;
			if (r.status == 200) px = imgDecodeMem((const u8*)r.body, r.len, TILE, TILE, &w, &h);
			if (px && (w != TILE || h != TILE)) {
				free(px);
				px = NULL;
			}
			httpFree(&r);
		}
		if (j->cancel) {
			free(px);
			break;
		}
		storeTile(z, x, y, px, px ? 1 : 2);
		n++;
	}
}

static void kick(void) {
	if (jobBusy(&s_job)) return;
	int z, x, y;
	if (!nextMissing(&z, &x, &y)) return;
	s_job.run = tileJob;
	netSubmit(&s_job);
}

static void setZoom(int z, s64 anchorX, s64 anchorY) {
	z = CLAMP(z, ZMIN, ZMAX);
	if (z == s_z) return;
	// mantem o ponto (anchor, relativo a tela) fixo ao trocar o zoom
	s64 wx = s_cx + anchorX, wy = s_cy + anchorY;
	int d = z - s_z;
	if (d > 0) {
		wx <<= d;
		wy <<= d;
	} else {
		wx >>= -d;
		wy >>= -d;
	}
	s_cx = wx - anchorX;
	s_cy = wy - anchorY;
	s_z = z;
	sndMove();
	gfxInvalidate(GFX_BOTH);
}

static void centerOn(double lat, double lon, int z) {
	s_z = z;
	lonlatToPx(lat, lon, z, &s_cx, &s_cy);
	gfxInvalidate(GFX_BOTH);
}

static void searchJob(NetJob* j) {
	char q[200], url[400];
	urlEncode(s_query, q, sizeof(q));
	snprintf(url, sizeof(url), "https://nominatim.openstreetmap.org/search?q=%s&format=json&limit=1&accept-language=pt-BR", q);
	HttpResp r;
	s_found[0] = 0;
	if (httpGet(j, url, &r, 32768) < 0) {
		j->result = -1;
		return;
	}
	char lat[24], lon[24];
	if (jsonStr(r.body, "lat", lat, sizeof(lat)) && jsonStr(r.body, "lon", lon, sizeof(lon))) {
		s_fLat = strtod(lat, NULL);
		s_fLon = strtod(lon, NULL);
		jsonStr(r.body, "display_name", s_found, sizeof(s_found));
		j->result = 0;
	} else {
		snprintf(j->err, sizeof(j->err), "Lugar n\xC3\xA3o encontrado");
		j->result = -2;
	}
	httpFree(&r);
}

static void onSearch(const char* text) {
	if (!text || !text[0]) return;
	snprintf(s_query, sizeof(s_query), "%s", text);
	if (jobBusy(&s_job)) s_job.cancel = true;
	s_search.run = searchJob;
	netSubmit(&s_search);
}

static void mEnter(void) {
	s_maxTiles = sysIsDSi() ? 16 : 6;
	if (!s_init) {
		s_init = true;
		double lat = -15.79, lon = -47.88;  // Brasilia, se nao houver localizacao
		if (g_wx.valid && g_wx.lat[0]) {
			lat = strtod(g_wx.lat, NULL);
			lon = strtod(g_wx.lon, NULL);
			s_homeLat = lat;
			s_homeLon = lon;
			s_haveHome = true;
		}
		centerOn(lat, lon, s_haveHome ? 13 : 4);
	}
	kick();
}

static void mLeave(void) {
	if (jobBusy(&s_job)) s_job.cancel = true;
	while (jobBusy(&s_job)) threadWaitForVBlank();
	s_job.state = JOB_IDLE;
	mutexLock(&s_mtx);
	for (int i = 0; i < 16; i++) {
		free(s_tiles[i].px);
		memset(&s_tiles[i], 0, sizeof(Tile));
	}
	mutexUnlock(&s_mtx);
}

static void mFrame(void) {
	s_tick++;
	if (s_job.state == JOB_DONE) {
		s_job.state = JOB_IDLE;
		gfxInvalidate(GFX_BOTH);
	}
	if (jobBusy(&s_job) && (g_frame % 8) == 0) gfxInvalidate(GFX_BOTH);
	if (s_search.state == JOB_DONE) {
		s_search.state = JOB_IDLE;
		if (s_search.result == 0) {
			s_mark = true;
			s_markLat = s_fLat;
			s_markLon = s_fLon;
			centerOn(s_fLat, s_fLon, 14);
			uiToast(s_found);
		} else {
			uiToast(s_search.err[0] ? s_search.err : "Falha na busca");
		}
	}
	if (g_in.down & KEY_B) {
		sndBack();
		appHome();
		return;
	}
	s64 ox = s_cx, oy = s_cy;
	int step = 40;
	if (g_in.rep & KEY_LEFT) s_cx -= step;
	if (g_in.rep & KEY_RIGHT) s_cx += step;
	if (g_in.rep & KEY_UP) s_cy -= step;
	if (g_in.rep & KEY_DOWN) s_cy += step;
	if (g_in.rep & KEY_R) setZoom(s_z + 1, 0, 0);
	if (g_in.rep & KEY_L) setZoom(s_z - 1, 0, 0);
	if (g_in.down & KEY_X) kbdOpen("Procurar lugar (cidade, rua, ponto)", s_query, 90, onSearch);
	if ((g_in.down & KEY_A) && s_haveHome) centerOn(s_homeLat, s_homeLon, MAX(s_z, 13));
	// botoes de zoom na tela de baixo
	if (g_in.tap && inRect(g_in.sx, g_in.sy, SCR_W - 34, 8, 28, 28)) setZoom(s_z + 1, 0, 0);
	else if (g_in.tap && inRect(g_in.sx, g_in.sy, SCR_W - 34, 40, 28, 28)) setZoom(s_z - 1, 0, 0);
	else if (g_in.tap && s_haveHome && inRect(g_in.sx, g_in.sy, SCR_W - 34, 72, 28, 28)) centerOn(s_homeLat, s_homeLon, MAX(s_z, 13));
	else {
		// arrastar
		if (g_in.tDown && g_in.ty < HINTS_Y) {
			s_drag = true;
			s_gcx = s_cx;
			s_gcy = s_cy;
		}
		if (s_drag && g_in.touch) {
			s_cx = s_gcx - (g_in.tx - g_in.sx);
			s_cy = s_gcy - (g_in.ty - g_in.sy);
			s_vel[0] = -g_in.dx * 256;
			s_vel[1] = -g_in.dy * 256;
		}
		if (s_drag && !g_in.touch) {
			s_drag = false;
			// toque duplo rapido = aproximar no ponto
			static u32 lastTap;
			if (g_in.tap) {
				if (g_frame - lastTap < 20) setZoom(s_z + 1, g_in.sx - VIEW_W / 2, (g_in.sy + SCR_H) - VIEW_H / 2);
				lastTap = g_frame;
			}
		}
	}
	if (!s_drag && (s_vel[0] || s_vel[1])) {
		s_cx += s_vel[0] / 256;
		s_cy += s_vel[1] / 256;
		s_vel[0] = s_vel[0] * 7 / 8;
		s_vel[1] = s_vel[1] * 7 / 8;
		if (abs(s_vel[0]) < 128) s_vel[0] = 0;
		if (abs(s_vel[1]) < 128) s_vel[1] = 0;
	}
	s64 world = (s64)TILE << s_z;
	s_cy = CLAMP(s_cy, VIEW_H / 2 - 64, world - VIEW_H / 2 + 64);
	s_cx = ((s_cx % world) + world) % world;
	if (s_cx != ox || s_cy != oy) gfxInvalidate(GFX_BOTH);
	kick();
}

// desenha a parte do mapa que cai no canvas (offY = linha da vista onde o canvas comeca)
static void drawMap(Canvas* c, int offY, int h) {
	s64 left = s_cx - VIEW_W / 2, top = s_cy - VIEW_H / 2 + offY;
	int lim = 1 << s_z;
	u16 bg = HEX(0xE8E4DA), grid = HEX(0xD6D1C4);
	cvSetClip(c, 0, 0, SCR_W, h);
	mutexLock(&s_mtx);
	for (s64 ty = top >> 8; (ty << 8) < top + h; ty++) {
		for (s64 tx = left >> 8; (tx << 8) < left + VIEW_W; tx++) {
			int sx = (int)((tx << 8) - left), sy = (int)((ty << 8) - top);
			if (ty < 0 || ty >= lim) {
				cvFill(c, sx, sy, TILE, TILE, HEX(0xAAD3DF));
				continue;
			}
			int wx = (int)(((tx % lim) + lim) % lim);
			Tile* t = findTile(s_z, wx, (int)ty);
			if (t) {
				t->used = s_tick;
				cvImage(c, t->px, TILE, TILE, sx, sy);
			} else {
				cvFill(c, sx, sy, TILE, TILE, bg);
				for (int k = 0; k < TILE; k += 32) {
					cvHLine(c, sx, sy + k, TILE, grid);
					cvVLine(c, sx + k, sy, TILE, grid);
				}
			}
		}
	}
	mutexUnlock(&s_mtx);
	// marcadores
	for (int m = 0; m < 2; m++) {
		double la = m ? s_markLat : s_homeLat, lo = m ? s_markLon : s_homeLon;
		if (m ? !s_mark : !s_haveHome) continue;
		s64 px, py;
		lonlatToPx(la, lo, s_z, &px, &py);
		int sx = (int)(px - left), sy = (int)(py - top);
		if (m) {
			cvIcon(c, IC_PIN_20, sx - 10, sy - 19, HEX(0xE5484D));
		} else {
			cvCircleA(c, sx, sy, 12, HEX(0x1E88E5), 8);
			cvCircle(c, sx, sy, 6, HEX(0xFFFFFF));
			cvCircle(c, sx, sy, 4, HEX(0x1E88E5));
		}
	}
	cvResetClip(c);
}

static void mDrawTop(Canvas* c) {
	drawMap(c, 0, SCR_H);
	// barra translucida
	cvFillA(c, 0, 0, SCR_W, 18, HEX(0xFFFFFF), 22);
	double lat, lon;
	pxToLonlat(s_cx, s_cy, s_z, &lat, &lon);
	char b[64];
	snprintf(b, sizeof(b), "%.4f, %.4f  \xE2\x80\xA2  zoom %d", lat, lon, s_z);
	cvText(c, FONT_SMALL, 6, 1, HEX(0x333333), b);
	if (jobBusy(&s_job) || jobBusy(&s_search)) uiSpinner(c, SCR_W - 12, 9, 5, HEX(0x333333));
}

static void mDrawBot(Canvas* c) {
	drawMap(c, SCR_H, HINTS_Y);
	// botoes flutuantes
	int bx = SCR_W - 34;
	int icons[3] = {IC_PLUS_20, IC_MINUS_20, IC_TARGET_20};
	for (int i = 0; i < (s_haveHome ? 3 : 2); i++) {
		cvShadow(c, bx, 8 + i * 32, 28, 28, 8, 8);
		cvRRect(c, bx, 8 + i * 32, 28, 28, 8, HEX(0xFFFFFF));
		cvIcon(c, icons[i], bx + 4, 12 + i * 32, HEX(0x333333));
	}
	cvFillA(c, 0, HINTS_Y - 12, 110, 12, HEX(0xFFFFFF), 20);
	cvText(c, FONT_SMALL, 3, HINTS_Y - 15, HEX(0x444444), "\xC2\xA9 OpenStreetMap");
	static const Hint h[] = {{KEY_L, "Zoom"}, {KEY_X, "Buscar"}, {KEY_A, "Eu"}, {KEY_B, "Voltar"}};
	uiHints(c, h, 4);
}

const App app_maps = {"Mapas", mEnter, mLeave, mFrame, mDrawTop, mDrawBot};

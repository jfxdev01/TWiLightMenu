// DSi Dash Racing — renderizacao 3D (hardware 3D do DS/DSi, tela de cima)
// Mundo em metros. Os vertices sao enviados relativos a camera, divididos por 64
// (v16 cobre +-512 m) e a matriz de modelo reescala x64.
#define RACE_DATA_IMPL
#include "race.h"
#include "race_tex_bin.h"
#include <math.h>

#define FAR_M 470
#define SCALE_SH 6  // /64

static int s_tex[TX_COUNT];
static s32 s_cx, s_cy, s_cz;       // camera em f32
static float s_fx, s_fz;           // frente da camera (xz)
static float s_rx, s_rz;           // direita da camera (xz)
static int s_poly;
static int s_frame;

// secoes transversais calculadas no frame
#define NOFS 6
static const s32 OFS[NOFS] = {
	-(s32)((RT_HALF_W + RT_CURB_W + RT_RUNOFF) * 4096), -(s32)((RT_HALF_W + RT_CURB_W) * 4096), -(s32)(RT_HALF_W * 4096),
	(s32)(RT_HALF_W * 4096), (s32)((RT_HALF_W + RT_CURB_W) * 4096), (s32)((RT_HALF_W + RT_CURB_W + RT_RUNOFF) * 4096),
};
static s32 s_px[RT_N][NOFS], s_pz[RT_N][NOFS];
static int s_calcFrame[RT_N];
static u16 s_vis[RT_N];
static int s_nvis;

// ---------------------------------------------------------------------------
// primitivas
// ---------------------------------------------------------------------------
static inline v16 rel(s32 v, s32 c) {
	s32 d = (v - c) >> SCALE_SH;
	if (d > 32767) d = 32767;
	if (d < -32767) d = -32767;
	return (v16)d;
}

static inline void vtx(s32 x, s32 y, s32 z) { glVertex3v16(rel(x, s_cx), rel(y, s_cy), rel(z, s_cz)); }

static inline void tc(int u, int v) { GFX_TEX_COORD = TEXTURE_PACK(inttot16(u), inttot16(v)); }

static inline s32 F(float v) { return (s32)(v * 4096.0f); }

static void bind(int t) { glBindTexture(0, t < 0 ? 0 : s_tex[t]); }

static void fmt(int id, bool fog, int alpha, bool cull) {
	glPolyFmt(POLY_ALPHA(alpha) | (cull ? POLY_CULL_BACK : POLY_CULL_NONE) | POLY_ID(id) | (fog ? POLY_FOG : 0));
}

int rwPolyCount(void) { return s_poly; }
int g_rwFps;
static void initTables(void);

// ---------------------------------------------------------------------------
// init / fim
// ---------------------------------------------------------------------------
static void loadTextures(void) {
	glResetTextures();
	const u8* blob = race_tex_bin;
	for (int i = 0; i < TX_COUNT; i++) {
		const RtTex* t = &g_rtTex[i];
		glGenTextures(1, &s_tex[i]);
		glBindTexture(0, s_tex[i]);
		int params = GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T;
		glTexImage2D(0, 0, (GL_TEXTURE_TYPE_ENUM)t->type, t->sx, t->sy, 0, params, blob + t->off);
		if (t->pal >= 0) glColorTableEXT(0, 0, 8, 0, 0, (const u16*)(blob + t->pal));
	}
}

void rwInit(void) {
	videoSetMode(MODE_5_3D);
	lcdMainOnTop();
	vramSetBankA(VRAM_A_TEXTURE);
	vramSetBankB(VRAM_B_TEXTURE);
	vramSetBankD(VRAM_D_MAIN_BG_0x06000000);
	vramSetBankE(VRAM_E_TEX_PALETTE);
	// HUD: bitmap 16 bits na frente do 3D (pixels com bit15 = 0 sao transparentes)
	int bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	bgSetPriority(bg, 0);
	REG_BG0CNT = BG_PRIORITY(1);
	bgUpdate();
	dmaFillWords(0, BG_GFX, 256 * 192 * 2);

	glInit();
	glEnable(GL_TEXTURE_2D | GL_ANTIALIAS | GL_BLEND | GL_FOG);
	glClearColor(24, 27, 30, 31);  // bruma do horizonte (fundo opaco para o antialias)
	glClearPolyID(63);
	glClearDepth(GL_MAX_DEPTH);
	glViewport(0, 0, 255, 191);
	// neblina (Z-buffer): a profundidade de 15 bits e nao-linear; calcula a tabela pela distancia
	// de cada degrau: z_ndc = (f+n)/(f-n) - 2fn/((f-n) d); prof = (z_ndc + 1) / 2 * 32767
	glFogColor(24, 27, 30, 31);
	const int FOG_OFF = 32640, FOG_SH = 8;  // degraus de 4 unidades
	glFogShift(FOG_SH);
	glFogOffset(FOG_OFF);
	const float n = 0.4f, f = (float)FAR_M;
	for (int i = 0; i < 32; i++) {
		float depth = FOG_OFF + i * (0x400 >> FOG_SH);
		float zn = depth * 2.0f / 32767.0f - 1.0f;
		float k = (f + n) / (f - n) - zn;
		float d = k > 1e-6f ? (2 * f * n / (f - n)) / k : 1e9f;
		float t = (d - 120.0f) / (f - 20.0f - 120.0f);
		t = t < 0 ? 0 : (t > 1 ? 1 : t);
		glFogDensity(i, (int)(t * 118));
	}
	loadTextures();
	initTables();
	glMaterialShinyness();
	s_frame = 1;
	memset(s_calcFrame, 0, sizeof(s_calcFrame));
}

void rwShutdown(void) {
	glResetTextures();
	gfxRestore2D();
}

// ---------------------------------------------------------------------------
// tabelas precalculadas (o ARM9 nao tem FPU: nada de seno/cosseno por frame)
// ---------------------------------------------------------------------------
#define SKY_SEG 12
#define HZ_SEG 16
static s32 s_skyC[SKY_SEG + 1], s_skyS[SKY_SEG + 1];  // f32 (4096 = 1)
static s32 s_hzX[HZ_SEG + 1], s_hzZ[HZ_SEG + 1];      // f32, raio 415 m
static s32 s_objC[RT_NOBJ], s_objS[RT_NOBJ];          // cos/sin do angulo de cada objeto
static s32 s_lhC[9], s_lhS[9];                        // farol (8 lados)
static u8 s_lhLit[8];
static s32 s_cloudX[8], s_cloudZ[8];
static s32 s_fxi, s_fzi, s_rxi, s_rzi;  // frente/direita da camera (f32)
#define RO_TYPES 7
static u16 s_objIdx[RT_NOBJ];            // objetos agrupados por tipo
static u16 s_objStart[RO_TYPES + 1];

static void initTables(void) {
	for (int k = 0; k <= SKY_SEG; k++) {
		float a = k * 6.2831853f / SKY_SEG;
		s_skyC[k] = F(cosf(a));
		s_skyS[k] = F(sinf(a));
	}
	for (int k = 0; k <= HZ_SEG; k++) {
		// angulo de rumo: 0 = norte, horario
		float a = k * 6.2831853f / HZ_SEG;
		s_hzX[k] = F(sinf(a) * 415);
		s_hzZ[k] = F(-cosf(a) * 415);
	}
	int n = 0;
	for (int t = 0; t < RO_TYPES; t++) {
		s_objStart[t] = n;
		for (int k = 0; k < RT_NOBJ; k++)
			if (g_rtObj[k].type == t) s_objIdx[n++] = k;
	}
	s_objStart[RO_TYPES] = n;
	for (int k = 0; k < RT_NOBJ; k++) {
		float a = g_rtObj[k].ang * 0.0174533f;
		s_objC[k] = F(cosf(a));
		s_objS[k] = F(sinf(a));
	}
	for (int k = 0; k <= 8; k++) {
		float a = k * 6.2831853f / 8;
		s_lhC[k] = F(cosf(a));
		s_lhS[k] = F(sinf(a));
	}
	for (int k = 0; k < 8; k++) s_lhLit[k] = (u8)(255 * (0.72f + 0.28f * cosf((k + 0.5f) * 6.2831853f / 8 - 2.4f)));
	static const s16 CA[8] = {20, 70, 130, 170, 215, 255, 300, 335};
	for (int k = 0; k < 8; k++) {
		float a = CA[k] * 0.0174533f;
		s_cloudX[k] = F(sinf(a) * 380);
		s_cloudZ[k] = F(-cosf(a) * 380);
	}
}

static inline s32 mulf(s32 a, s32 b) { return (s32)(((s64)a * b) >> 12); }

// ---------------------------------------------------------------------------
// camera
// ---------------------------------------------------------------------------
static void setupCamera(void) {
	s_cx = F(R.camX);
	s_cy = F(R.camY);
	s_cz = F(R.camZ);
	float dx = R.camTX - R.camX, dy = R.camTY - R.camY, dz = R.camTZ - R.camZ;
	float l = sqrtf(dx * dx + dz * dz);
	if (l < 0.01f) l = 0.01f;
	s_fx = dx / l;
	s_fz = dz / l;
	s_rx = -s_fz;
	s_rz = s_fx;
	s_fxi = F(s_fx);
	s_fzi = F(s_fz);
	s_rxi = F(s_rx);
	s_rzi = F(s_rz);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(R.fov, 256.0f / 192.0f, 0.4f, (float)FAR_M);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAtf32(0, 0, 0, F(dx), F(dy), F(dz), 0, inttof32(1), 0);
	// sol (direcao dos raios, em coordenadas do mundo)
	glLight(0, RGB15(31, 30, 27), floattov10(0.42f), floattov10(-0.78f), floattov10(0.46f));
	glLight(1, RGB15(7, 9, 13), floattov10(-0.3f), floattov10(-0.2f), floattov10(-0.93f));
	glScalef32(inttof32(64), inttof32(64), inttof32(64));
}

// dentro do alcance e na frente da camera (so inteiros)
static inline bool visible(s32 x, s32 z, s32 marginM, s32 farM) {
	s32 dx = (x - s_cx) >> 12, dz = (z - s_cz) >> 12;
	if (dx > farM || dx < -farM || dz > farM || dz < -farM) return false;
	if (dx * dx + dz * dz > farM * farM) return false;
	return ((dx * s_fxi + dz * s_fzi) >> 12) > -marginM;
}

// ---------------------------------------------------------------------------
// ceu, montanhas, sol, nuvens
// ---------------------------------------------------------------------------
static void drawSky(void) {
	bind(-1);
	fmt(0, false, 31, false);
	glBegin(GL_QUADS);
	const s32 yLow = s_cy - F(15), yMid = s_cy + F(95), yTop = s_cy + F(330);
	for (int k = 0; k < SKY_SEG; k++) {
		s32 c0 = s_skyC[k], s0 = s_skyS[k], c1 = s_skyC[k + 1], s1 = s_skyS[k + 1];
		// so os gomos na frente da camera
		if ((c0 + c1) * (s_fxi >> 6) + (s0 + s1) * (s_fzi >> 6) < -(4096 * 64 / 2)) continue;
		// faixa baixa: horizonte -> azul medio
		glColor3b(190, 214, 236);
		vtx(s_cx + c0 * 440, yLow, s_cz + s0 * 440);
		vtx(s_cx + c1 * 440, yLow, s_cz + s1 * 440);
		glColor3b(120, 172, 228);
		vtx(s_cx + c1 * 430, yMid, s_cz + s1 * 430);
		vtx(s_cx + c0 * 430, yMid, s_cz + s0 * 430);
		// faixa alta: -> zenite
		glColor3b(120, 172, 228);
		vtx(s_cx + c0 * 430, yMid, s_cz + s0 * 430);
		vtx(s_cx + c1 * 430, yMid, s_cz + s1 * 430);
		glColor3b(58, 118, 212);
		vtx(s_cx + c1 * 200, yTop, s_cz + s1 * 200);
		vtx(s_cx + c0 * 200, yTop, s_cz + s0 * 200);
		s_poly += 2;
	}
	glEnd();
}

// quad de frente para a camera (tudo em f32)
static void billboardI(s32 x, s32 y, s32 z, s32 w, s32 h, int u0, int v0, int u1, int v1) {
	s32 hx = mulf(s_rxi, w >> 1), hz = mulf(s_rzi, w >> 1);
	tc(u0, v1);
	vtx(x - hx, y, z - hz);
	tc(u1, v1);
	vtx(x + hx, y, z + hz);
	tc(u1, v0);
	vtx(x + hx, y + h, z + hz);
	tc(u0, v0);
	vtx(x - hx, y + h, z - hz);
	s_poly++;
}

static void drawHorizon(void) {
	// montanhas/skyline: cilindro que acompanha a camera, textura presa ao mundo
	bind(TX_MOUNT);
	fmt(1, true, 31, false);
	glColor3b(255, 255, 255);
	glBegin(GL_QUADS);
	const s32 y0 = -F(4), y1 = F(78);
	for (int k = 0; k < HZ_SEG; k++) {
		s32 x0 = s_hzX[k], z0 = s_hzZ[k], x1 = s_hzX[k + 1], z1 = s_hzZ[k + 1];
		// so desenha o que esta na frente
		if (((((x0 + x1) >> 12) * s_fxi + ((z0 + z1) >> 12) * s_fzi) >> 12) < -300) continue;
		int u0 = k * 16, u1 = u0 + 16;
		tc(u0, 64);
		vtx(s_cx + x0, y0, s_cz + z0);
		tc(u0, 0);
		vtx(s_cx + x0, y1, s_cz + z0);
		tc(u1, 0);
		vtx(s_cx + x1, y1, s_cz + z1);
		tc(u1, 64);
		vtx(s_cx + x1, y0, s_cz + z1);
		s_poly++;
	}
	glEnd();
}

static void drawSunClouds(void) {
	// sol (oposto aos raios de luz)
	bind(TX_GLOW);
	fmt(2, false, 30, false);
	glBegin(GL_QUADS);
	s32 sx = s_cx - F(0.42f * 400), sy = s_cy + F(0.78f * 400), sz = s_cz - F(0.46f * 400);
	glColor3b(255, 244, 200);
	billboardI(sx, sy - F(40), sz, F(90), F(90), 0, 0, 32, 32);
	glColor3b(255, 255, 240);
	billboardI(sx, sy - F(12), sz, F(30), F(30), 0, 0, 32, 32);
	glEnd();
	// nuvens
	bind(TX_CLOUD);
	fmt(3, false, 22, false);
	glBegin(GL_QUADS);
	glColor3b(255, 255, 255);
	static const s16 CH[8] = {180, 210, 170, 230, 190, 200, 175, 220};
	static const s16 CS[8] = {70, 90, 60, 100, 80, 70, 110, 80};
	for (int k = 0; k < 8; k++) {
		if ((s_cloudX[k] >> 12) * s_fxi + (s_cloudZ[k] >> 12) * s_fzi < 0) continue;
		s32 w = inttof32(CS[k]);
		billboardI(s_cx + s_cloudX[k], inttof32(CH[k]) + (s_cy >> 2), s_cz + s_cloudZ[k], w * 7 / 5, w >> 1, 0, 0, 64, 32);
	}
	glEnd();
}

// ---------------------------------------------------------------------------
// terreno
// ---------------------------------------------------------------------------
static u16 s_cellList[4][RT_GW * RT_GH];
static int s_cellN[4];

static void drawGround(void) {
	static const u8 TEX[4] = {TX_GRASS, TX_SAND, TX_WATER, TX_CITY};
	static const s16 REP[4] = {1024, 1024, 512, 1024};  // texels por celula de 64 m
	int wshift = ((int)(R.waterT * 10)) & 63;
	// so as celulas num quadrado em volta da camera, separadas por tipo numa passada
	int cxm = (s_cx >> 12) - RT_GX0, czm = (s_cz >> 12) - RT_GZ0;
	int gx0 = MAX(0, (cxm - FAR_M) / RT_CELL - 1), gx1 = MIN(RT_GW - 1, (cxm + FAR_M) / RT_CELL + 1);
	int gz0 = MAX(0, (czm - FAR_M) / RT_CELL - 1), gz1 = MIN(RT_GH - 1, (czm + FAR_M) / RT_CELL + 1);
	s_cellN[0] = s_cellN[1] = s_cellN[2] = s_cellN[3] = 0;
	for (int gz = gz0; gz <= gz1; gz++) {
		s32 cz = inttof32(RT_GZ0 + gz * RT_CELL + RT_CELL / 2);
		for (int gx = gx0; gx <= gx1; gx++) {
			s32 cx = inttof32(RT_GX0 + gx * RT_CELL + RT_CELL / 2);
			if (!visible(cx, cz, 64, FAR_M + 40)) continue;
			int t = g_rtCells[gz][gx] & 3;
			s_cellList[t][s_cellN[t]++] = gz * RT_GW + gx;
		}
	}
	for (int type = 0; type < 4; type++) {
		if (!s_cellN[type]) continue;
		bind(TEX[type]);
		fmt(4 + type, true, 31, true);
		glColor3b(255, 255, 255);
		glBegin(GL_QUADS);
		int r = REP[type];
		int o = type == 2 ? wshift : 0;
		s32 y = type == 2 ? -F(0.45f) : -F(0.12f);
		for (int n = 0; n < s_cellN[type]; n++) {
			int gz = s_cellList[type][n] / RT_GW, gx = s_cellList[type][n] % RT_GW;
			s32 x0 = inttof32(RT_GX0 + gx * RT_CELL), z0 = inttof32(RT_GZ0 + gz * RT_CELL);
			// celulas perto da camera sao divididas em 4x4 (poligonos enormes perdem precisao de profundidade)
			s32 dxm = ((x0 - s_cx) >> 12) + RT_CELL / 2, dzm = ((z0 - s_cz) >> 12) + RT_CELL / 2;
			s32 d2 = dxm * dxm + dzm * dzm;
			int div = d2 < 80 * 80 ? 4 : (d2 < 150 * 150 ? 2 : 1);
			s32 st = inttof32(RT_CELL) / div;
			int rs = r / div;
			for (int a = 0; a < div; a++)
				for (int b = 0; b < div; b++) {
					s32 qx0 = x0 + a * st, qz0 = z0 + b * st, qx1 = qx0 + st, qz1 = qz0 + st;
					int u0 = o + a * rs, v0 = b * rs;
					tc(u0, v0);
					vtx(qx0, y, qz0);
					tc(u0, v0 + rs);
					vtx(qx0, y, qz1);
					tc(u0 + rs, v0 + rs);
					vtx(qx1, y, qz1);
					tc(u0 + rs, v0);
					vtx(qx1, y, qz0);
					s_poly++;
				}
		}
		glEnd();
	}
	// agua alem da grade (horizonte do mar a oeste)
	bind(TX_WATER);
	fmt(6, true, 31, true);
	glBegin(GL_QUADS);
	for (int k = 0; k < 6; k++) {
		s32 x0 = inttof32(RT_GX0 - 400), x1 = inttof32(RT_GX0);
		s32 z0 = inttof32(RT_GZ0 + k * 256), z1 = z0 + inttof32(256);
		if (!visible((x0 + x1) / 2, (z0 + z1) / 2, 200, FAR_M + 250)) continue;
		tc(wshift, 0);
		vtx(x0, -F(0.3f), z0);
		tc(wshift, 1024);
		vtx(x0, -F(0.3f), z1);
		tc(wshift + 1024, 1024);
		vtx(x1, -F(0.3f), z1);
		tc(wshift + 1024, 0);
		vtx(x1, -F(0.3f), z0);
		s_poly++;
	}
	glEnd();
}

// ---------------------------------------------------------------------------
// pista
// ---------------------------------------------------------------------------
static void calcSection(int i) {
	if (s_calcFrame[i] == s_frame) return;
	s_calcFrame[i] = s_frame;
	const RtSec* s = &g_rt[i];
	for (int k = 0; k < NOFS; k++) {
		s_px[i][k] = s->x + (s32)(((s64)s->rx * OFS[k]) >> 12);
		s_pz[i][k] = s->z + (s32)(((s64)s->rz * OFS[k]) >> 12);
	}
}

static s32 s_secD2[RT_N];  // distancia^2 (m^2) das secoes visiveis
static int s_visFrame[RT_N];
static u8 s_span[RT_N];    // secoes cobertas pelo quad que comeca em i (0 = coberta pelo anterior)
#define LOD_M 170          // alem disso, a pista e desenhada de 2 em 2 secoes

static void findVisible(void) {
	s_nvis = 0;
	for (int i = 0; i < RT_N; i++) {
		if (visible(g_rt[i].x, g_rt[i].z, 40, FAR_M)) {
			s32 dx = (g_rt[i].x - s_cx) >> 12, dz = (g_rt[i].z - s_cz) >> 12;
			s_secD2[i] = dx * dx + dz * dz;
			s_visFrame[i] = s_frame;
			s_vis[s_nvis++] = i;
			calcSection(i);
			calcSection((i + 1) % RT_N);
		}
	}
	for (int n = 0; n < s_nvis; n++) {
		int i = s_vis[n];
		s_span[i] = 1;
		if (!(i & 1) && i < RT_N - 2 && s_secD2[i] > LOD_M * LOD_M) {
			s_span[i] = 2;
			calcSection(i + 2);
		}
	}
	for (int n = 0; n < s_nvis; n++) {
		int i = s_vis[n];
		if ((i & 1) && s_visFrame[i - 1] == s_frame && s_span[i - 1] == 2) s_span[i] = 0;
	}
}

// quad entre as colunas k0..k1 das secoes i e j (virado para cima)
static inline void strip(int i, int j, int k0, int k1, s32 y, int u0, int u1, int v0, int v1) {
	tc(u0, v0);
	vtx(s_px[i][k0], y, s_pz[i][k0]);
	tc(u1, v0);
	vtx(s_px[i][k1], y, s_pz[i][k1]);
	tc(u1, v1);
	vtx(s_px[j][k1], y, s_pz[j][k1]);
	tc(u0, v1);
	vtx(s_px[j][k0], y, s_pz[j][k0]);
	s_poly++;
}

static void drawTrack(void) {
	const s32 yRoad = F(0.08f), yCurb = F(0.11f), yRun = F(0.04f);
	// asfalto
	bind(TX_ROAD);
	fmt(10, true, 31, true);
	glColor3b(255, 255, 255);
	glBegin(GL_QUADS);
	for (int n = 0; n < s_nvis; n++) {
		int i = s_vis[n], sp = s_span[i];
		if (!sp) continue;
		int j = (i + sp) % RT_N;
		int v0 = (i & 1) * 64;
		strip(i, j, 2, 3, yRoad, 0, 128, v0 + 64 * sp, v0);
	}
	glEnd();
	// zebras
	bind(TX_CURB);
	fmt(11, true, 31, true);
	glBegin(GL_QUADS);
	for (int n = 0; n < s_nvis; n++) {
		int i = s_vis[n], sp = s_span[i];
		if (!sp || !(g_rt[i].flags & 1) || s_secD2[i] > 260 * 260) continue;
		int j = (i + sp) % RT_N;
		int v0 = (i & 1) * 64;
		strip(i, j, 1, 2, yCurb, 0, 8, v0 + 64 * sp, v0);
		strip(i, j, 3, 4, yCurb, 0, 8, v0 + 64 * sp, v0);
	}
	glEnd();
	// areas de escape (grama ou areia), mais a faixa da zebra quando nao ha zebra
	for (int pass = 0; pass < 2; pass++) {
		bind(pass ? TX_SAND : TX_GRASS);
		fmt(12 + pass, true, 31, true);
		glColor3b(255, 255, 255);
		glBegin(GL_QUADS);
		// grama sobre grama quase nao aparece de longe: so perto; areia ate 330 m
		const s32 lim = pass ? 330 * 330 : 220 * 220;
		for (int n = 0; n < s_nvis; n++) {
			int i = s_vis[n], sp = s_span[i];
			if (!sp || s_secD2[i] > lim) continue;
			int j = (i + sp) % RT_N;
			u8 fl = g_rt[i].flags;
			bool sandL = (fl >> 3) & 1, sandR = (fl >> 4) & 1;
			const int rep = 128 * sp;  // texels por secao (8 m)
			if (sandL == pass) strip(i, j, 0, (fl & 1) ? 1 : 2, yRun, 0, 200, rep, 0);
			if (sandR == pass) strip(i, j, (fl & 1) ? 4 : 3, 5, yRun, 0, 200, rep, 0);
		}
		glEnd();
	}
	// barreiras de pneus do lado de fora das curvas
	bind(TX_TIRES);
	fmt(14, true, 31, true);
	glColor3b(255, 255, 255);
	glBegin(GL_QUADS);
	const s32 hb = F(1.1f);
	for (int n = 0; n < s_nvis; n++) {
		int i = s_vis[n], sp = s_span[i];
		u8 fl = g_rt[i].flags;
		if (!sp || s_secD2[i] > 300 * 300) continue;
		int j = (i + sp) % RT_N, u1 = 64 * sp;
		if (fl & 2) {  // esquerda: face virada para a pista (+direita)
			tc(0, 16);
			vtx(s_px[i][0], 0, s_pz[i][0]);
			tc(u1, 16);
			vtx(s_px[j][0], 0, s_pz[j][0]);
			tc(u1, 0);
			vtx(s_px[j][0], hb, s_pz[j][0]);
			tc(0, 0);
			vtx(s_px[i][0], hb, s_pz[i][0]);
			s_poly++;
		}
		if (fl & 4) {
			tc(0, 16);
			vtx(s_px[j][5], 0, s_pz[j][5]);
			tc(u1, 16);
			vtx(s_px[i][5], 0, s_pz[i][5]);
			tc(u1, 0);
			vtx(s_px[i][5], hb, s_pz[i][5]);
			tc(0, 0);
			vtx(s_px[j][5], hb, s_pz[j][5]);
			s_poly++;
		}
	}
	glEnd();
	// linha de largada (quadriculado)
	int st = RT_START_SEG;
	if (s_calcFrame[st] == s_frame) {
		const RtSec* s = &g_rt[st];
		bind(TX_CHECKER);
		fmt(15, true, 31, true);
		glBegin(GL_QUADS);
		s32 ax = s->x, az = s->z;
		s32 fx = s->tx * 3 / 2, fz = s->tz * 3 / 2;  // 1,5 m para frente e para tras
		s32 wx = (s32)(((s64)s->rx * OFS[3]) >> 12), wz = (s32)(((s64)s->rz * OFS[3]) >> 12);
		const s32 y = F(0.2f);
		tc(0, 24);
		vtx(ax - wx - fx, y, az - wz - fz);
		tc(160, 24);
		vtx(ax + wx - fx, y, az + wz - fz);
		tc(160, 0);
		vtx(ax + wx + fx, y, az + wz + fz);
		tc(0, 0);
		vtx(ax - wx + fx, y, az - wz + fz);
		s_poly++;
		glEnd();
	}
	// setas de turbo
	bind(TX_ARROW);
	fmt(16, true, 31, true);
	glBegin(GL_QUADS);
	for (int b = 0; b < RT_NBOOST; b++) {
		int i = g_rtBoost[b][0];
		if (!visible(g_rt[i].x, g_rt[i].z, 10, 250)) continue;
		const RtSec* s = &g_rt[i];
		s32 lat = g_rtBoost[b][1] * 4096 / 10;
		s32 cx = s->x + (s32)(((s64)s->rx * lat) >> 12), cz = s->z + (s32)(((s64)s->rz * lat) >> 12);
		s32 fx = s->tx * 2, fz = s->tz * 2;              // 2 m
		s32 wx = s->rx * 17 / 10, wz = s->rz * 17 / 10;  // 1,7 m
		const s32 y = F(0.2f);
		tc(0, 32);
		vtx(cx - wx - fx, y, cz - wz - fz);
		tc(32, 32);
		vtx(cx + wx - fx, y, cz + wz - fz);
		tc(32, 0);
		vtx(cx + wx + fx, y, cz + wz + fz);
		tc(0, 0);
		vtx(cx - wx + fx, y, cz - wz + fz);
		s_poly++;
	}
	glEnd();
}

// ---------------------------------------------------------------------------
// objetos (posicoes em f32, direcoes das tabelas)
// ---------------------------------------------------------------------------
static const u8 BOX_SH[4] = {255, 200, 170, 225};  // sombreamento por face

// caixa alinhada aos eixos (predios): faces laterais texturizadas
static void boxAxis(s32 x, s32 z, s32 hw, s32 h, int texU, int texV) {
	s32 px[4] = {x - hw, x + hw, x + hw, x - hw}, pz[4] = {z - hw, z - hw, z + hw, z + hw};
	int u1 = (hw >> 11) * texU, v1 = (h >> 12) * texV;
	for (int k = 0; k < 4; k++) {
		int a = k, b = (k + 1) & 3;  // face de a para b, virada para fora
		glColor3b(BOX_SH[k], BOX_SH[k], BOX_SH[k]);
		tc(0, v1);
		vtx(px[b], 0, pz[b]);
		tc(u1, v1);
		vtx(px[a], 0, pz[a]);
		tc(u1, 0);
		vtx(px[a], h, pz[a]);
		tc(0, 0);
		vtx(px[b], h, pz[b]);
		s_poly++;
	}
}

// caixa orientada sem textura (pilares do portico)
static void boxRot(s32 x, s32 z, s32 hw, s32 h, s32 ca, s32 sa) {
	static const s8 SX[4] = {-1, 1, 1, -1}, SZ[4] = {-1, -1, 1, 1};
	s32 px[4], pz[4];
	for (int k = 0; k < 4; k++) {
		s32 lx = SX[k] * hw, lz = SZ[k] * hw;
		px[k] = x + mulf(lx, ca) - mulf(lz, sa);
		pz[k] = z + mulf(lx, sa) + mulf(lz, ca);
	}
	for (int k = 0; k < 4; k++) {
		int a = k, b = (k + 1) & 3;
		glColor3b(BOX_SH[k], BOX_SH[k], BOX_SH[k]);
		vtx(px[b], 0, pz[b]);
		vtx(px[a], 0, pz[a]);
		vtx(px[a], h, pz[a]);
		vtx(px[b], h, pz[b]);
		s_poly++;
	}
}

static void drawObjects(void) {
	// palmeiras e arbustos (sempre de frente para a camera)
	for (int pass = 0; pass < 2; pass++) {
		bind(pass ? TX_BUSH : TX_PALM);
		fmt(20 + pass, true, 31, false);
		glColor3b(255, 255, 255);
		glBegin(GL_QUADS);
		int t = pass ? RO_BUSH : RO_PALM;
		for (int n = s_objStart[t]; n < s_objStart[t + 1]; n++) {
			const RtObj* o = &g_rtObj[s_objIdx[n]];
			s32 x = inttof32(o->x), z = inttof32(o->z);
			if (!visible(x, z, 12, pass ? 220 : 380)) continue;
			s32 h = inttof32(o->p1);
			if (pass) billboardI(x, 0, z, h + (h * 3 / 10), h, 0, 0, 32, 32);
			else billboardI(x, 0, z, h * 62 / 100, h, 0, 0, 64, 128);
		}
		glEnd();
	}
	// predios
	bind(TX_BUILDING);
	fmt(22, true, 31, true);
	glBegin(GL_QUADS);
	static u8 bVis[RT_NOBJ];
	for (int n = s_objStart[RO_BUILDING]; n < s_objStart[RO_BUILDING + 1]; n++) {
		const RtObj* o = &g_rtObj[s_objIdx[n]];
		s32 x = inttof32(o->x), z = inttof32(o->z);
		bVis[n] = visible(x, z, 60, FAR_M);
		if (!bVis[n]) continue;
		boxAxis(x, z, inttof32(o->p1) >> 1, inttof32(o->p2), 4, 4);
	}
	glEnd();
	// telhados (so dos predios mais baixos que a camera)
	bind(-1);
	fmt(23, true, 31, true);
	glBegin(GL_QUADS);
	glColor3b(150, 150, 158);
	for (int n = s_objStart[RO_BUILDING]; n < s_objStart[RO_BUILDING + 1]; n++) {
		const RtObj* o = &g_rtObj[s_objIdx[n]];
		if (!bVis[n] || inttof32(o->p2) < s_cy) continue;
		s32 x = inttof32(o->x), z = inttof32(o->z);
		s32 hw = inttof32(o->p1) >> 1, y = inttof32(o->p2);
		vtx(x - hw, y, z - hw);
		vtx(x - hw, y, z + hw);
		vtx(x + hw, y, z + hw);
		vtx(x + hw, y, z - hw);
		s_poly++;
	}
	glEnd();
	for (int n = s_objStart[RO_STAND]; n < s_objStart[RO_TYPES]; n++) {
		int k = s_objIdx[n];
		const RtObj* o = &g_rtObj[k];
		s32 ox = inttof32(o->x), oz = inttof32(o->z);
		if (o->type == RO_STAND) {
			// arquibancada: face inclinada com a torcida
			if (!visible(ox, oz, 40, 380)) continue;
			s32 lx = s_objS[k] * 20, lz = -s_objC[k] * 20;  // ao longo da pista (20 m)
			s32 fx = -o->p1 * s_objC[k], fz = -o->p1 * s_objS[k];  // em direcao a pista
			s32 dx = fx * 12, dz = fz * 12;                  // profundidade 12 m
			bind(TX_CROWD);
			fmt(24, true, 31, false);
			glColor3b(255, 255, 255);
			glBegin(GL_QUADS);
			tc(0, 96);
			vtx(ox - lx, F(0.8f), oz - lz);
			tc(320, 96);
			vtx(ox + lx, F(0.8f), oz + lz);
			tc(320, 0);
			vtx(ox + lx - dx, F(9), oz + lz - dz);
			tc(0, 0);
			vtx(ox - lx - dx, F(9), oz - lz - dz);
			s_poly++;
			glEnd();
			bind(-1);
			fmt(25, true, 31, false);
			glBegin(GL_QUADS);
			glColor3b(235, 235, 240);  // cobertura
			vtx(ox - lx + fx, F(12.5f), oz - lz + fz);
			vtx(ox + lx + fx, F(12.5f), oz + lz + fz);
			vtx(ox + lx - dx, F(13.5f), oz + lz - dz);
			vtx(ox - lx - dx, F(13.5f), oz - lz - dz);
			glColor3b(120, 124, 132);  // fundo
			vtx(ox - lx - dx, 0, oz - lz - dz);
			vtx(ox + lx - dx, 0, oz + lz - dz);
			vtx(ox + lx - dx, F(13.5f), oz + lz - dz);
			vtx(ox - lx - dx, F(13.5f), oz - lz - dz);
			glColor3b(90, 94, 100);  // base frontal
			vtx(ox - lx, 0, oz - lz);
			vtx(ox + lx, 0, oz + lz);
			vtx(ox + lx, F(0.8f), oz + lz);
			vtx(ox - lx, F(0.8f), oz - lz);
			s_poly += 3;
			glEnd();
		} else if (o->type == RO_AD) {
			// placa de propaganda (14 m)
			if (!visible(ox, oz, 20, 320)) continue;
			s32 ax = s_objS[k] * 7, az = -s_objC[k] * 7;
			bind(TX_AD0 + o->p1);
			fmt(26, true, 31, false);
			glColor3b(255, 255, 255);
			glBegin(GL_QUADS);
			tc(0, 32);
			vtx(ox - ax, F(1.2f), oz - az);
			tc(128, 32);
			vtx(ox + ax, F(1.2f), oz + az);
			tc(128, 0);
			vtx(ox + ax, F(4.7f), oz + az);
			tc(0, 0);
			vtx(ox - ax, F(4.7f), oz - az);
			s_poly++;
			glEnd();
		} else if (o->type == RO_GANTRY) {
			// portico de largada
			if (!visible(ox, oz, 20, 420)) continue;
			s32 wx = mulf(s_objC[k], F(RT_HALF_W + 3)), wz = mulf(s_objS[k], F(RT_HALF_W + 3));
			bind(TX_BANNER);
			fmt(27, true, 31, false);
			glColor3b(255, 255, 255);
			glBegin(GL_QUADS);
			tc(0, 32);
			vtx(ox - wx, F(6.2f), oz - wz);
			tc(128, 32);
			vtx(ox + wx, F(6.2f), oz + wz);
			tc(128, 0);
			vtx(ox + wx, F(9.2f), oz + wz);
			tc(0, 0);
			vtx(ox - wx, F(9.2f), oz - wz);
			s_poly++;
			glEnd();
			bind(-1);
			fmt(28, true, 31, true);
			glBegin(GL_QUADS);
			boxRot(ox - wx, oz - wz, F(0.5f), F(9.2f), s_objC[k], s_objS[k]);
			boxRot(ox + wx, oz + wz, F(0.5f), F(9.2f), s_objC[k], s_objS[k]);
			glEnd();
		} else if (o->type == RO_LIGHTHOUSE) {
			// farol listrado
			if (!visible(ox, oz, 30, FAR_M)) continue;
			bind(-1);
			fmt(29, true, 31, true);
			glBegin(GL_QUADS);
			for (int band = 0; band < 5; band++) {
				s32 y0 = inttof32(band * 5), y1 = y0 + inttof32(5);
				s32 r0 = F(3.2f) - band * F(0.25f), r1 = r0 - F(0.25f);
				for (int s = 0; s < 8; s++) {
					int l = s_lhLit[s];
					if (band & 1) glColor3b(l * 235 >> 8, l * 235 >> 8, l * 235 >> 8);
					else glColor3b(l * 210 >> 8, l * 40 >> 8, l * 40 >> 8);
					vtx(ox + mulf(s_lhC[s + 1], r0), y0, oz + mulf(s_lhS[s + 1], r0));
					vtx(ox + mulf(s_lhC[s], r0), y0, oz + mulf(s_lhS[s], r0));
					vtx(ox + mulf(s_lhC[s], r1), y1, oz + mulf(s_lhS[s], r1));
					vtx(ox + mulf(s_lhC[s + 1], r1), y1, oz + mulf(s_lhS[s + 1], r1));
					s_poly++;
				}
			}
			glEnd();
		}
	}
}
// ---------------------------------------------------------------------------
// carros
// ---------------------------------------------------------------------------
static void drawCar(const Car* c, int idx) {
	s32 wx = F(c->x), wz = F(c->z);
	if (!visible(wx, wz, 12, 360)) return;
	s32 dxm = (wx - s_cx) >> 12, dzm = (wz - s_cz) >> 12, d2 = dxm * dxm + dzm * dzm;
	bool near = d2 < 110 * 110;  // rodas so de perto
	bool smooth = d2 < 45 * 45;  // normais por vertice so de perto (de longe, uma por face)
	// trigonometria inteira (tabela da libnds): 32768 = 360 graus
	s16 aH = (s16)((int)((c->h + c->slip) * 5215.19f) & 0x7FFF);
	s16 aR = (s16)((int)(c->roll * 5215.19f) & 0x7FFF), aP = (s16)((int)(c->pitch * 5215.19f) & 0x7FFF);
	s32 ch = cosLerp(aH), sh = sinLerp(aH), cr = cosLerp(aR), sr = sinLerp(aR), cp = cosLerp(aP), sp = sinLerp(aP);
	// linhas = imagens dos eixos do modelo: arfagem (x), rolagem (z), rumo (y);
	// x do modelo -> direita (ch, sh); z do modelo -> tras (-sh, ch)
	s32 x1 = -mulf(cp, sr), x2 = mulf(sp, sr);
	m4x3 m;
	m.m[0] = mulf(cr, ch);
	m.m[1] = sr;
	m.m[2] = mulf(cr, sh);
	m.m[3] = mulf(x1, ch) - mulf(sp, sh);
	m.m[4] = mulf(cp, cr);
	m.m[5] = mulf(x1, sh) + mulf(sp, ch);
	m.m[6] = mulf(x2, ch) - mulf(cp, sh);
	m.m[7] = -mulf(sp, cr);
	m.m[8] = mulf(x2, sh) + mulf(cp, ch);
	m.m[9] = (wx - s_cx) >> SCALE_SH;
	m.m[10] = (F(c->hop + 0.02f) - s_cy) >> SCALE_SH;
	m.m[11] = (wz - s_cz) >> SCALE_SH;
	glPushMatrix();
	glMultMatrix4x3(&m);
	glScalef32(64, 64, 64);  // volta para metros
	bind(-1);
	glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | POLY_ID(40 + idx) | POLY_FOG | POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1);
	u16 col = c->color;
	int cr5 = col & 31, cg5 = (col >> 5) & 31, cb5 = (col >> 10) & 31;
	int lastMat = -1;
	glBegin(GL_QUADS);
	for (int p = 0; p < CAR_NPOLY; p++) {
		const CarPoly* cp_ = &g_carPoly[p];
		if (!near && cp_->mat >= MAT_TIRE) break;
		if (cp_->mat != lastMat) {
			lastMat = cp_->mat;
			switch (lastMat) {
				case MAT_BODY:
					glMaterialf(GL_DIFFUSE, col);
					glMaterialf(GL_AMBIENT, RGB15(cr5 * 9 / 20, cg5 * 9 / 20, cb5 * 9 / 20));
					glMaterialf(GL_SPECULAR, BIT(15) | RGB15(18, 18, 18));
					glMaterialf(GL_EMISSION, RGB15(cr5 / 8, cg5 / 8, cb5 / 8));
					break;
				case MAT_GLASS:
					glMaterialf(GL_DIFFUSE, RGB15(2, 3, 5));
					glMaterialf(GL_AMBIENT, RGB15(2, 3, 4));
					glMaterialf(GL_SPECULAR, BIT(15) | RGB15(16, 17, 19));
					glMaterialf(GL_EMISSION, RGB15(1, 1, 2));
					break;
				case MAT_TRIM:
					glMaterialf(GL_DIFFUSE, RGB15(5, 5, 6));
					glMaterialf(GL_AMBIENT, RGB15(3, 3, 3));
					glMaterialf(GL_SPECULAR, BIT(15) | RGB15(8, 8, 8));
					glMaterialf(GL_EMISSION, RGB15(1, 1, 1));
					break;
				case MAT_HEAD:
					glMaterialf(GL_DIFFUSE, RGB15(8, 8, 8));
					glMaterialf(GL_AMBIENT, RGB15(4, 4, 4));
					glMaterialf(GL_SPECULAR, RGB15(0, 0, 0));
					glMaterialf(GL_EMISSION, RGB15(29, 29, 24));
					break;
				case MAT_TAIL:
					glMaterialf(GL_DIFFUSE, RGB15(10, 2, 2));
					glMaterialf(GL_AMBIENT, RGB15(6, 1, 1));
					glMaterialf(GL_SPECULAR, RGB15(0, 0, 0));
					glMaterialf(GL_EMISSION, c->braking ? RGB15(31, 6, 4) : RGB15(18, 2, 2));
					break;
				case MAT_TIRE:
					glMaterialf(GL_DIFFUSE, RGB15(3, 3, 3));
					glMaterialf(GL_AMBIENT, RGB15(2, 2, 2));
					glMaterialf(GL_SPECULAR, RGB15(2, 2, 2));
					glMaterialf(GL_EMISSION, RGB15(1, 1, 1));
					break;
				case MAT_RIM:
					glMaterialf(GL_DIFFUSE, RGB15(22, 22, 24));
					glMaterialf(GL_AMBIENT, RGB15(10, 10, 11));
					glMaterialf(GL_SPECULAR, BIT(15) | RGB15(24, 24, 24));
					glMaterialf(GL_EMISSION, RGB15(2, 2, 2));
					break;
			}
		}
		if (smooth) {
			for (int v = 0; v < 4; v++) {
				glNormal(cp_->normal[v]);
				glVertex3v16(cp_->v[v][0], cp_->v[v][1], cp_->v[v][2]);
			}
		} else {
			glNormal(cp_->normal[1]);
			for (int v = 0; v < 4; v++) glVertex3v16(cp_->v[v][0], cp_->v[v][1], cp_->v[v][2]);
		}
		s_poly++;
	}
	glEnd();
	glPopMatrix(1);
}

static void drawShadows(void) {
	bind(TX_SHADOW);
	fmt(60, false, 26, true);
	glColor3b(255, 255, 255);
	glBegin(GL_QUADS);
	for (int k = 0; k < R.nCars; k++) {
		const Car* c = &R.car[k];
		if (!visible(F(c->x), F(c->z), 10, 200)) continue;
		s16 aH = (s16)((int)((c->h + c->slip) * 5215.19f) & 0x7FFF);
		s32 sn = sinLerp(aH), cs = cosLerp(aH);
		s32 sc = F(1.0f - c->hop * 0.3f);
		s32 fx = mulf(sn, mulf(sc, F(2.7f))), fz = -mulf(cs, mulf(sc, F(2.7f)));
		s32 rx = mulf(cs, mulf(sc, F(1.35f))), rz = mulf(sn, mulf(sc, F(1.35f)));
		const s32 y = F(0.12f);
		s32 x = F(c->x + 0.2f), z = F(c->z + 0.25f);
		tc(0, 32);
		vtx(x - rx - fx, y, z - rz - fz);
		tc(32, 32);
		vtx(x + rx - fx, y, z + rz - fz);
		tc(32, 0);
		vtx(x + rx + fx, y, z + rz + fz);
		tc(0, 0);
		vtx(x - rx + fx, y, z - rz + fz);
		s_poly++;
	}
	glEnd();
}

static void drawParticles(void) {
	bind(TX_SPARK);
	for (int k = 0; k < MAX_PART; k++) {
		const Part* p = &R.part[k];
		if (p->life <= 0) continue;
		if (!visible(F(p->x), F(p->z), 5, 150)) continue;
		int a = (int)(p->alpha * (p->life / p->maxLife));
		if (a < 2) continue;
		glPolyFmt(POLY_ALPHA(a > 31 ? 31 : a) | POLY_CULL_NONE | POLY_ID(61));
		glBegin(GL_QUADS);
		glColor(p->color);
		float s = p->size * (0.6f + 0.4f * (p->life / p->maxLife));
		billboardI(F(p->x), F(p->y - s * 0.5f), F(p->z), F(s), F(s), 0, 0, 16, 16);
		glEnd();
	}
}

void rwRender(void) {
	static int cnt;
	static u64 t0;
	u64 now = tickGetCount();
	cnt++;
	if (now - t0 >= TICK_FREQ) {
		g_rwFps = cnt;
		cnt = 0;
		t0 = now;
	}
	s_frame++;
	s_poly = 0;
	setupCamera();
	drawSky();
	drawHorizon();
	drawSunClouds();
	drawGround();
	findVisible();
	drawTrack();
	drawObjects();
	for (int k = R.nCars - 1; k >= 0; k--) drawCar(&R.car[k], k);
	drawShadows();
	drawParticles();
	glFlush(0);  // Z-buffer: interpola corretamente em poligonos grandes
}

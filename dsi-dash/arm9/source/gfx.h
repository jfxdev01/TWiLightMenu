// Renderizacao por software em framebuffers RGB555 (um por tela).
#pragma once
#include <nds.h>

#define SCR_W 256
#define SCR_H 192

typedef struct Canvas {
	u16* px;                 // stride = SCR_W
	int cx0, cy0, cx1, cy1;  // clip (x1/y1 exclusivos)
	int which;               // GFX_TOP / GFX_BOT
} Canvas;

enum { GFX_TOP = 1, GFX_BOT = 2, GFX_BOTH = 3 };

extern Canvas g_top, g_bot;

#define COL8(r, g, b) ((u16)(0x8000 | ((((b) >> 3) & 31) << 10) | ((((g) >> 3) & 31) << 5) | (((r) >> 3) & 31)))
#define HEX(c) COL8(((c) >> 16) & 255, ((c) >> 8) & 255, (c) & 255)

void gfxInit(void);
void gfxInvalidate(int which);
int  gfxTakeRedraw(void);         // retorna e limpa as telas a redesenhar
void gfxMarkDrawn(int which);     // telas desenhadas neste frame (copiar no proximo VBlank)
void gfxPresent(void);
void gfxPresentRows(int which, int y0, int y1);  // limita a proxima copia a faixas de linhas
void gfxRestore2D(void);
void gfxBrightness(int level);  // -16 (preto) .. 0 .. 16 (branco), ambas as telas

static inline u16 blend(u16 d, u16 s, int a) {  // a: 0..32
	u32 dd = (d | ((u32)d << 16)) & 0x03E07C1F;
	u32 ss = (s | ((u32)s << 16)) & 0x03E07C1F;
	u32 r = ((ss * a + dd * (32 - a)) >> 5) & 0x03E07C1F;
	return (u16)(r | (r >> 16)) | 0x8000;
}
u16 lerpColor(u16 a, u16 b, int t256);

void cvSetClip(Canvas* c, int x, int y, int w, int h);
void cvResetClip(Canvas* c);
void cvClear(Canvas* c, u16 col);
void cvFill(Canvas* c, int x, int y, int w, int h, u16 col);
void cvFillA(Canvas* c, int x, int y, int w, int h, u16 col, int a);
void cvGrad(Canvas* c, int x, int y, int w, int h, u16 top, u16 bottom);
void cvHLine(Canvas* c, int x, int y, int w, u16 col);
void cvVLine(Canvas* c, int x, int y, int h, u16 col);
void cvRRect(Canvas* c, int x, int y, int w, int h, int r, u16 col);
void cvRRectA(Canvas* c, int x, int y, int w, int h, int r, u16 col, int a);
void cvRRectGrad(Canvas* c, int x, int y, int w, int h, int r, u16 top, u16 bottom);
void cvRRectBorder(Canvas* c, int x, int y, int w, int h, int r, int t, u16 col);
void cvShadow(Canvas* c, int x, int y, int w, int h, int r, int a);
void cvCircle(Canvas* c, int cx, int cy, int r, u16 col);
void cvCircleA(Canvas* c, int cx, int cy, int r, u16 col, int a);
void cvRing(Canvas* c, int cx, int cy, int r, int t, u16 col);
void cvLine(Canvas* c, int x0, int y0, int x1, int y1, u16 col);  // AA (Wu), 1px
void cvIcon(Canvas* c, int id, int x, int y, u16 col);
void cvIconA(Canvas* c, int id, int x, int y, u16 col, int a);
int  icW(int id);
int  icH(int id);
void cvImage(Canvas* c, const u16* img, int iw, int ih, int x, int y);
void cvImageScaled(Canvas* c, const u16* img, int iw, int ih, int x, int y, int w, int h);

// texto (UTF-8; tambem aceita Latin-1 cru)
int  fontHeight(int f);
int  fontAscent(int f);
u32  utf8Next(const char** s);
int  textWidth(int f, const char* s);
int  textWidthN(int f, const char* s, int nbytes);
int  cvText(Canvas* c, int f, int x, int y, u16 col, const char* s);
int  cvTextN(Canvas* c, int f, int x, int y, u16 col, const char* s, int nbytes);
void cvTextC(Canvas* c, int f, int cx, int y, u16 col, const char* s);
void cvTextR(Canvas* c, int f, int rx, int y, u16 col, const char* s);
void cvTextFit(Canvas* c, int f, int x, int y, int maxw, u16 col, const char* s);
int  textFitBytes(int f, const char* s, int maxw);  // quantos bytes cabem em maxw
int  cvTextWrap(Canvas* c, int f, int x, int y, int w, int maxLines, u16 col, const char* s);  // retorna altura

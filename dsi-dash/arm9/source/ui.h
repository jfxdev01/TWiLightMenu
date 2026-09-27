// Tema, entrada e componentes de interface (estilo Switch)
#pragma once
#include <nds.h>
#include "gfx.h"

typedef struct Theme {
	bool dark;
	u16 bg, surface, surface2, text, text2, line, accent, accent2, onAccent, danger, ok;
} Theme;
extern Theme T;
void themeApply(bool dark);

typedef struct Input {
	u32 down, held, up, rep;  // rep = down + auto-repeat
	bool touch, tDown, tUp, tap, drag;
	int tx, ty;   // posicao atual (ou ultima)
	int sx, sy;   // inicio do toque
	int dx, dy;   // delta desde o frame anterior
	int frames;   // frames desde o inicio do toque
} Input;
extern Input g_in;
extern u32 g_frame;
void inputUpdate(void);
bool inRect(int x, int y, int rx, int ry, int rw, int rh);
bool tapIn(int rx, int ry, int rw, int rh);

void uiResetAreas(void);  // chamado antes de redesenhar a tela de baixo
u16 uiFavColor(void);
int uiButtonGlyphW(u32 key);

// selecao animada (borda ciano pulsante)
u16 uiSelColor(void);
void uiSelBorder(Canvas* c, int x, int y, int w, int h, int r);

// barras
#define STATUS_H 22
#define HINTS_Y 172
void uiStatusBar(Canvas* c, bool overlay);  // overlay = sobre fundo colorido (texto branco)
typedef struct Hint {
	u32 key;
	const char* label;
} Hint;
void uiHints(Canvas* c, const Hint* h, int n);
void uiButtonGlyph(Canvas* c, int x, int y, u32 key);  // circulo com letra
void uiBackButton(Canvas* c);  // botao "<" no canto superior esquerdo da tela de baixo

// componentes
void uiSpinner(Canvas* c, int cx, int cy, int r, u16 col);
void uiToast(const char* msg);
void uiDrawToast(Canvas* c);
bool uiToastActive(void);
void uiButton(Canvas* c, int x, int y, int w, int h, const char* label, bool primary, bool selected);
void uiHeader(Canvas* c, int icon, const char* title);  // cabecalho de app (tela de cima, abaixo da status bar)
void uiEmpty(Canvas* c, int icon, const char* msg, const char* sub);
void uiProgress(Canvas* c, int x, int y, int w, int pct);

// dialogo modal simples (ate 4 opcoes)
typedef void (*DialogFn)(int choice);  // -1 = cancelado
void uiDialog(const char* title, const char* msg, const char* const* opts, int nopts, DialogFn fn);
bool uiDialogActive(void);
void uiDialogFrame(void);
void uiDialogDraw(Canvas* c);

// lista rolavel
typedef struct ListView ListView;
typedef void (*ListDrawFn)(Canvas* c, ListView* lv, int idx, int x, int y, int w, int h, bool sel);
struct ListView {
	int x, y, w, h;
	int rowH;
	int count;
	int sel;
	int scroll;     // pixels
	int vel;        // 8.8
	bool dragging;
	int grabScroll;
	ListDrawFn draw;
	void* user;
};
void lvInit(ListView* lv, int x, int y, int w, int h, int rowH, int count, ListDrawFn fn);
int  lvUpdate(ListView* lv);  // retorna indice ativado (A/toque) ou -1
void lvDraw(Canvas* c, ListView* lv);
void lvEnsureVisible(ListView* lv);

// teclado na tela (estilo Switch). Abre modal; callback recebe texto ou NULL
typedef void (*KeyboardFn)(const char* text);
void kbdOpen(const char* title, const char* initial, int maxLen, KeyboardFn fn);
bool kbdActive(void);
void kbdFrame(void);
void kbdDrawTop(Canvas* c);
void kbdDrawBot(Canvas* c);

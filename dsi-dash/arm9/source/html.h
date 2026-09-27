// Documento HTML simplificado: parser + layout + desenho (modo leitura)
#pragma once
#include <nds.h>
#include "gfx.h"

#define DOC_MARGIN 6

enum { DC_TEXT, DC_LINK, DC_HEAD, DC_MUTED, DC_CODE };
enum { RF_UNDER = 1, RF_RULE = 2, RF_IMGBOX = 4 };
enum { FT_TEXT, FT_PASSWORD, FT_HIDDEN, FT_SUBMIT, FT_CHECKBOX, FT_RADIO, FT_TEXTAREA };
enum { FK_LINK, FK_FIELD };

typedef struct DocRun {
	s32 y;
	s16 x, w;
	u8 font, color, flags, h;
	s16 link;
	u32 off;
	u16 len;
} DocRun;

typedef struct DocField {
	u8 type, checked;
	s16 form;
	u32 nameOff;
	char* value;   // malloc
	s32 y;
	s16 x, w, h;
} DocField;

typedef struct DocForm {
	u32 actionOff;
	bool post;
} DocForm;

typedef struct DocImg {
	u32 srcOff;
	s32 y;
	s16 x, w, h;
	s16 link;
	u16* px;       // RGB555 (w*h), pertence ao cache do navegador; NULL = nao carregada
	bool placed;   // tem caixa no layout
} DocImg;

typedef struct DocFocus {
	u8 kind;
	s16 idx;
	s32 y;
	s16 x;
} DocFocus;

typedef struct Doc {
	char title[160];
	char url[1024];
	char* pool;
	int poolLen, poolCap;
	DocRun* runs;
	int nRuns, capRuns;
	u32* links;    // offsets no pool
	int nLinks, capLinks;
	DocField* fields;
	int nFields, capFields;
	DocForm* forms;
	int nForms, capForms;
	DocImg* imgs;
	int nImgs, capImgs;
	DocFocus* focus;
	int nFocus, capFocus;
	int height;
} Doc;

// busca uma imagem ja decodificada (cache do navegador)
typedef bool (*DocImgLookup)(const char* url, u16** px, int* w, int* h);
Doc* docParseHtml(const char* html, int len, const char* url, const char* ctype);
Doc* docParseHtmlEx(const char* html, int len, const char* url, const char* ctype, DocImgLookup lookup);
Doc* docFromText(const char* txt, int len, const char* url);
void docFree(Doc* d);
static inline const char* docStr(const Doc* d, u32 off) { return d->pool + off; }
void docDraw(Canvas* c, const Doc* d, int scroll, int viewY, int viewH, int focus);
int docHit(const Doc* d, int px, int py);          // coords da pagina -> indice de foco ou -1
void docFocusBox(const Doc* d, int f, int* y0, int* y1);
int docBuildSubmit(const Doc* d, int form, int submitField, char* out, int outSz, bool* post);  // query string
const char* docFocusHref(const Doc* d, int f);     // href se o foco for link
int htmlToText(const char* in, int n, char* out, int sz, bool latin1);

// Registro de apps + dados compartilhados (clima)
#pragma once
#include <nds.h>
#include <time.h>
#include "gfx.h"

typedef struct App {
	const char* name;
	void (*enter)(void);
	void (*leave)(void);
	void (*frame)(void);
	void (*drawTop)(Canvas* c);
	void (*drawBot)(Canvas* c);
} App;

enum {
	APP_HOME,
	APP_WEATHER,
	APP_SETTINGS,
	APP_WIFI,
	APP_BROWSER,
	APP_NEWS,
	APP_WIKI,
	APP_CAMERA,
	APP_ALBUM,
	APP_TRANSFER,
	APP_FILES,
	APP_NOTES,
	APP_CALC,
	APP_MAPS,
	APP_TRANSLATE,
	APP_RACE,
	APP_COUNT
};

extern const App* g_apps[APP_COUNT];
void appOpen(int id);
void appHome(void);
int appCurrent(void);

// apps
extern const App app_home, app_weather, app_settings, app_wifi, app_browser, app_news, app_wiki,
	app_camera, app_album, app_transfer, app_files, app_notes, app_calc, app_maps, app_translate, app_race;

// abre o navegador direto numa URL (http, https ou file:caminho)
void browserOpenUrl(const char* url);
// cria uma nota nova com o texto
bool notesAdd(const char* text);

// ---------------------------------------------------------------------------
// clima (wx.c)
// ---------------------------------------------------------------------------
#define WX_DAYS 7
#define WX_HOURS 24

typedef struct Weather {
	bool valid;
	time_t fetched;         // sysNow() do ultimo sucesso
	char city[48], region[48], cc[8], tz[48], ip[20];
	char lat[16], lon[16];
	int temp10, feels10, hum, wind10, code, isDay, precip10;
	int dCount;
	int dMax10[WX_DAYS], dMin10[WX_DAYS], dCode[WX_DAYS], dPop[WX_DAYS];
	int dY[WX_DAYS], dM[WX_DAYS], dD[WX_DAYS];
	char sunrise[6], sunset[6];
	int hCount;
	int hTemp10[WX_HOURS], hPop[WX_HOURS], hCode[WX_HOURS], hHour[WX_HOURS], hDay[WX_HOURS];
} Weather;

extern Weather g_wx;
void wxInit(void);
void wxRefresh(void);
void wxTick(void);
bool wxBusy(void);
const char* wxError(void);
const char* wmoText(int code);
const char* wmoShort(int code);
void wxDrawIcon(Canvas* c, int code, bool day, int x, int y, bool big, bool onLight);
void wxSkyColors(int code, bool day, u16* top, u16* bottom);
void fmtTemp(char* out, int sz, int t10);  // "25°"

// ---------------------------------------------------------------------------
// JSON minimo (json.c)
// ---------------------------------------------------------------------------
bool jsonStr(const char* json, const char* key, char* out, int outSize);
bool jsonDouble(const char* json, const char* key, double* out);
bool jsonInt(const char* json, const char* key, int* out);
bool jsonArrNum(const char* block, const char* key, int idx, double* out);
bool jsonArrStr(const char* block, const char* key, int idx, char* out, int sz);
const char* jsonSkipTo(const char* json, const char* key);  // posicao apos "key":
int jsonUnescape(const char* in, int inLen, char* out, int outSz);

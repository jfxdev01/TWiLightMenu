// DSi Dash — sistema: relogio sincronizado, configuracoes, hardware, energia, sons
#include "common.h"
#include <dirent.h>
#include <fat.h>
#include <sys/stat.h>
#include "../../common/ipc.h"
#include "bearssl.h"

Settings g_set;

static char s_root[8];
static bool s_storage;
static char s_nick[40];
static unsigned s_batt = 15;
static int s_backlight = -1;
static bool s_exit;
static bool s_arm7ok;

// relogio
static long s_clockOffset;       // somar a time(NULL) para obter hora local
static int s_utcOffset;
static bool s_haveUtcOff, s_haveDate, s_synced;
static time_t s_dateUtc, s_dateAtDevice;

// ---------------------------------------------------------------------------
// datas
// ---------------------------------------------------------------------------
long sysDaysFromCivil(int y, int m, int d) {
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long)doe - 719468;
}

time_t sysParseHttpDate(const char* s) {
	static const char* MON = "JanFebMarAprMayJunJulAugSepOctNovDec";
	int day, year, hh, mm, ss;
	char mon[8] = {0};
	if (sscanf(s, "%*3s, %d %7s %d %d:%d:%d", &day, mon, &year, &hh, &mm, &ss) != 6) return (time_t)-1;
	const char* mp = strstr(MON, mon);
	if (!mp || strlen(mon) != 3) return (time_t)-1;
	int month = (int)((mp - MON) / 3) + 1;
	return (time_t)(sysDaysFromCivil(year, month, day) * 86400L + hh * 3600L + mm * 60L + ss);
}

static void resync(void) {
	if (g_set.netClock && s_haveDate && s_haveUtcOff) {
		s_clockOffset = (long)(s_dateUtc + s_utcOffset) - (long)s_dateAtDevice;
		s_synced = true;
	}
}

void sysOnHttpDate(const char* hdr, long age) {
	if (!hdr || !hdr[0]) return;
	time_t utc = sysParseHttpDate(hdr);
	if (utc == (time_t)-1) return;
	if (age > 0 && age < 400 * 86400) utc += age;
	s_dateUtc = utc;
	s_dateAtDevice = time(NULL);
	s_haveDate = true;
	resync();
}

void sysSetUtcOffset(int seconds) {
	s_utcOffset = seconds;
	s_haveUtcOff = true;
	resync();
}

bool sysClockSynced(void) { return s_synced && g_set.netClock; }

bool sysHaveNetTime(void) { return s_haveDate; }

time_t sysUtcNow(void) {
	if (s_haveDate) return s_dateUtc + (time(NULL) - s_dateAtDevice);
	return time(NULL) - (s_haveUtcOff ? s_utcOffset : 0);  // RTC do console (pode estar errado)
}

// ---------------------------------------------------------------------------
// entropia para o TLS: mistura temporizacoes, ruido do toque/RSSI e o RTC num SHA-256
// ---------------------------------------------------------------------------
static br_sha256_context s_ent;
static bool s_entInit;

void sysEntropyAdd(const void* d, int n) {
	if (!s_entInit) {
		br_sha256_init(&s_ent);
		s_entInit = true;
	}
	br_sha256_update(&s_ent, d, n);
}

static void entropySample(void) {
	struct {
		u64 tick;
		u16 t[4];
		u16 vcount;
		u32 keys;
		u16 tx, ty;
		u32 rssi;
		u32 frame;
	} e;
	e.tick = tickGetCount();
	for (int i = 0; i < 4; i++) e.t[i] = TIMER_DATA(i);
	e.vcount = REG_VCOUNT;
	e.keys = keysHeld();
	TouchData td;
	touchRead(&td);
	e.tx = td.rawx;
	e.ty = td.rawy;
	e.rssi = wlmgrGetRssi();
	e.frame = g_frame;
	sysEntropyAdd(&e, sizeof(e));
}

void sysEntropyGet(u8 out[32]) {
	entropySample();
	br_sha256_context c = s_ent;
	br_sha256_out(&c, out);
	sysEntropyAdd(out, 32);  // catraca: saidas seguintes diferentes
	u8 z = 0x5A;
	sysEntropyAdd(&z, 1);
}

time_t sysNow(void) { return (time_t)((long)time(NULL) + (g_set.netClock ? s_clockOffset : 0)); }

void sysLocalTm(struct tm* out) {
	time_t t = sysNow();
	gmtime_r(&t, out);  // o epoch ja esta em hora local
}

void sysFormatTime(char* out, int sz) {
	struct tm t;
	sysLocalTm(&t);
	if (g_set.clock24) {
		snprintf(out, sz, "%02d:%02d", t.tm_hour, t.tm_min);
	} else {
		int h = t.tm_hour % 12;
		snprintf(out, sz, "%d:%02d %s", h ? h : 12, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
	}
}

static const char* s_wd[] = {"domingo", "segunda-feira", "ter\xC3\xA7" "a-feira", "quarta-feira", "quinta-feira", "sexta-feira", "s\xC3\xA1" "bado"};
static const char* s_wdS[] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "S\xC3\xA1" "b"};
static const char* s_mon[] = {"janeiro", "fevereiro", "mar\xC3\xA7o", "abril", "maio", "junho", "julho", "agosto", "setembro", "outubro", "novembro", "dezembro"};

const char* sysWeekdayShort(int wd) { return (wd >= 0 && wd < 7) ? s_wdS[wd] : "?"; }
const char* sysMonthName(int m) { return (m >= 0 && m < 12) ? s_mon[m] : "?"; }

void sysFormatDate(char* out, int sz, const struct tm* t) {
	snprintf(out, sz, "%s, %d de %s", s_wd[t->tm_wday % 7], t->tm_mday, s_mon[t->tm_mon % 12]);
}

// ---------------------------------------------------------------------------
// configuracoes (INI simples)
// ---------------------------------------------------------------------------
static void setDefaults(void) {
	memset(&g_set, 0, sizeof(g_set));
	g_set.dark = false;
	g_set.clock24 = true;
	g_set.netClock = true;
	g_set.sounds = true;
	g_set.images = true;
	g_set.backlight = -1;
	strcpy(g_set.homepage, "about:home");
	strcpy(g_set.newsFeed, "http://feeds.bbci.co.uk/portuguese/rss.xml");
	strcpy(g_set.wikiLang, "pt");
}

bool sysHasStorage(void) { return s_storage; }
const char* sysRoot(void) { return s_root; }

void sysDataPath(char* out, int sz, const char* name) { snprintf(out, sz, "%s_nds/dsidash/%s", s_root, name); }

static void loadSettings(void) {
	if (!s_storage) return;
	char path[96];
	sysDataPath(path, sizeof(path), "settings.ini");
	FILE* f = fopen(path, "r");
	if (!f) return;
	char line[320];
	while (fgets(line, sizeof(line), f)) {
		char* eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		char* v = eq + 1;
		v[strcspn(v, "\r\n")] = 0;
		const char* k = line;
		if (!strcmp(k, "dark")) g_set.dark = atoi(v);
		else if (!strcmp(k, "clock24")) g_set.clock24 = atoi(v);
		else if (!strcmp(k, "netClock")) g_set.netClock = atoi(v);
		else if (!strcmp(k, "sounds")) g_set.sounds = atoi(v);
		else if (!strcmp(k, "images")) g_set.images = atoi(v);
		else if (!strcmp(k, "backlight")) g_set.backlight = atoi(v);
		else if (!strcmp(k, "manualLoc")) g_set.manualLoc = atoi(v);
		else if (!strcmp(k, "city")) snprintf(g_set.city, sizeof(g_set.city), "%s", v);
		else if (!strcmp(k, "lat")) snprintf(g_set.lat, sizeof(g_set.lat), "%s", v);
		else if (!strcmp(k, "lon")) snprintf(g_set.lon, sizeof(g_set.lon), "%s", v);
		else if (!strcmp(k, "homepage")) snprintf(g_set.homepage, sizeof(g_set.homepage), "%s", v);
		else if (!strcmp(k, "newsFeed")) snprintf(g_set.newsFeed, sizeof(g_set.newsFeed), "%s", v);
		else if (!strcmp(k, "wikiLang")) snprintf(g_set.wikiLang, sizeof(g_set.wikiLang), "%s", v);
	}
	fclose(f);
}

void sysSaveSettings(void) {
	if (!s_storage) return;
	char path[96];
	sysDataPath(path, sizeof(path), "settings.ini");
	FILE* f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "dark=%d\nclock24=%d\nnetClock=%d\nsounds=%d\nbacklight=%d\n", g_set.dark, g_set.clock24, g_set.netClock, g_set.sounds, g_set.backlight);
	fprintf(f, "manualLoc=%d\ncity=%s\nlat=%s\nlon=%s\n", g_set.manualLoc, g_set.city, g_set.lat, g_set.lon);
	fprintf(f, "homepage=%s\nnewsFeed=%s\nwikiLang=%s\n", g_set.homepage, g_set.newsFeed, g_set.wikiLang);
	fclose(f);
}

static bool dirExists(const char* p) {
	DIR* d = opendir(p);
	if (!d) return false;
	closedir(d);
	return true;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
static void readNick(void) {
	const EnvUserSettings* us = g_envUserSettings;
	int n = MIN(us->user.name_len, 10);
	char* o = s_nick;
	for (int i = 0; i < n; i++) {
		u16 c = us->user.name_ucs2[i];
		if (c < 0x80) *o++ = (char)c;
		else if (c < 0x800) {
			*o++ = 0xC0 | (c >> 6);
			*o++ = 0x80 | (c & 0x3F);
		} else {
			*o++ = 0xE0 | (c >> 12);
			*o++ = 0x80 | ((c >> 6) & 0x3F);
			*o++ = 0x80 | (c & 0x3F);
		}
	}
	*o = 0;
	if (!s_nick[0]) strcpy(s_nick, "Jogador");
}

u32 sysIpc(unsigned cmd, u32 arg) {
	if (!s_arm7ok) return 0;
	return pxiSendAndReceive(PxiChannel_User0, IPC_MSG(cmd, arg));
}

void sysInit(void) {
	setDefaults();
	readNick();
	{
		// memoria nao inicializada + RTC + configuracoes do usuario
		void* junk = malloc(8192);
		if (junk) {
			sysEntropyAdd(junk, 8192);
			free(junk);
		}
		time_t t = time(NULL);
		sysEntropyAdd(&t, sizeof(t));
		sysEntropyAdd(g_envUserSettings, sizeof(EnvUserSettings));
	}

	pxiWaitRemote(PxiChannel_User0);
	s_arm7ok = true;

	soundInit();
	soundPowerOn();

	if (fatInitDefault()) {
		if (dirExists("sd:/")) strcpy(s_root, "sd:/");
		else if (dirExists("fat:/")) strcpy(s_root, "fat:/");
		s_storage = s_root[0] != 0;
	}
	if (s_storage) {
		char p[96];
		snprintf(p, sizeof(p), "%s_nds", s_root);
		mkdir(p, 0777);
		snprintf(p, sizeof(p), "%s_nds/dsidash", s_root);
		mkdir(p, 0777);
		loadSettings();
	}
	s_batt = pmGetBatteryState();
	if (sysIsDSi()) {
		u32 r = sysIpc(IPC_BACKLIGHT_GET, 0);
		s_backlight = r ? (int)r - 1 : -1;
		if (g_set.backlight >= 0 && s_backlight >= 0 && g_set.backlight != s_backlight) sysSetBacklight(g_set.backlight);
	}
}

static int s_sndT[3];
static int s_tick;

void sysTick(void) {
	entropySample();
	if (++s_tick % 120 == 0) {
		unsigned b = pmGetBatteryState();
		if (b != s_batt) {
			s_batt = b;
			gfxInvalidate(GFX_TOP);
		}
	}
	// minuto mudou -> redesenha a tela de cima (relogio)
	static int lastMin = -1;
	time_t now = sysNow();
	int m = (int)(now / 60);
	if (m != lastMin) {
		lastMin = m;
		gfxInvalidate(GFX_TOP);
	}
	// envelope dos sons
	for (int i = 0; i < 3; i++) {
		if (s_sndT[i] > 0 && --s_sndT[i] == 0) soundStop(1u << (8 + i));
	}
}

bool sysIsDSi(void) { return systemIsTwlMode(); }
int sysRamMB(void) { return sysIsDSi() ? 16 : 4; }
const char* sysNick(void) { return s_nick; }
int sysFavColorIndex(void) { return g_envUserSettings->user.favorite_color & 15; }
unsigned sysBattery(void) { return s_batt; }

int sysGetBacklight(void) { return s_backlight; }

void sysSetBacklight(int lvl) {
	if (!sysIsDSi() || s_backlight < 0) return;
	lvl = CLAMP(lvl, 0, 4);
	if (sysIpc(IPC_BACKLIGHT_SET, lvl)) s_backlight = lvl;
	g_set.backlight = s_backlight;
}

// ---------------------------------------------------------------------------
// energia
// ---------------------------------------------------------------------------
bool sysHasLoader(void) { return pmHasResetJumpTarget(); }
void sysRequestExit(void) { s_exit = true; }
bool sysExitRequested(void) { return s_exit; }

ITCM_CODE static void parkArm9(void) {
	REG_IME = 0;
	for (;;) __asm__ volatile("mcr p15, 0, %0, c7, c0, 4" ::"r"(0));
}

bool sysRebootTo(const char* sdPath) {
	if (!sysIsDSi()) return false;
	static u8 blk[0x400] __attribute__((aligned(32)));
	memset(blk, 0, sizeof(blk));
	memcpy(blk, "AutoLoadInfo", 12);
	*(u16*)(blk + 0x0C) = 0x3F0;
	*(u32*)(blk + 0x10) = BIT(0) | BIT(1);  // carregar titulo em 0x838, usar cores
	u16 bgc = T.dark ? 0 : 0x7FFF;
	*(u16*)(blk + 0x14) = bgc;
	*(u16*)(blk + 0x16) = bgc;
	// caminho em UTF-16: "sd:/x" -> "sdmc:/x"
	const char* p = sdPath;
	if (!strncmp(p, "sd:", 3)) p += 3;
	u16* dst = (u16*)(blk + 0x38);
	const char* pre = "sdmc:";
	int n = 0;
	while (*pre && n < 0x103) dst[n++] = (u8)*pre++;
	while (*p && n < 0x103) dst[n++] = (u8)*p++;
	*(u16*)(blk + 0x0E) = swiCRC16(0xFFFF, blk + 0x10, 0x3F0);
	DC_FlushAll();
	if (!sysIpc(IPC_REBOOT_UNLAUNCH, IPC_PTRARG(blk))) return false;
	parkArm9();
	return true;
}

void sysShutdown(void) {
	if (sysIsDSi() && sysIpc(IPC_SHUTDOWN, 0)) parkArm9();
	systemShutDown();
}

// ---------------------------------------------------------------------------
// sons (PSG, canais 8..10)
// ---------------------------------------------------------------------------
static void tone(int ch, int hz, int vol, int frames) {
	if (!g_set.sounds) return;
	soundPreparePsg((8 + ch) | SOUND_START, vol, 64, soundTimerFromHz(8 * hz), SoundDuty_50);
	s_sndT[ch] = frames;
}

void sndClick(void) {
	tone(0, 1568, 260, 3);
	tone(1, 2093, 200, 5);
}
void sndMove(void) { tone(2, 1046, 150, 2); }
void sndBack(void) {
	tone(0, 1175, 220, 3);
	tone(1, 784, 200, 5);
}

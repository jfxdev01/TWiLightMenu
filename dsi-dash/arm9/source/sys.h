// Estado do sistema: relogio, configuracoes, bateria, armazenamento, energia
#pragma once
#include <nds.h>
#include <time.h>

typedef struct Settings {
	bool dark;
	bool clock24;
	bool netClock;
	bool sounds;
	bool images;       // imagens no navegador
	int backlight;     // 0..4 (DSi)
	bool manualLoc;    // cidade escolhida manualmente
	char city[48];
	char lat[16], lon[16];
	char homepage[256];
	char newsFeed[256];
	char wikiLang[8];
} Settings;
extern Settings g_set;

void sysInit(void);
void sysTick(void);
void sysSaveSettings(void);

bool sysHasStorage(void);
const char* sysRoot(void);            // "sd:/" / "fat:/" / ""
void sysDataPath(char* out, int sz, const char* name);  // <root>_nds/dsidash/<name>
bool sysIsDSi(void);
int  sysRamMB(void);

// relogio (hora local)
time_t sysNow(void);
void sysLocalTm(struct tm* out);
void sysOnHttpDate(const char* hdr, long age);  // header "Date:" (UTC) + "Age:" de caches
void sysSetUtcOffset(int seconds);
bool sysClockSynced(void);
bool sysHaveNetTime(void);
time_t sysUtcNow(void);
void sysEntropyAdd(const void* d, int n);
void sysEntropyGet(u8 out[32]);
void sysFormatTime(char* out, int sz);
void sysFormatDate(char* out, int sz, const struct tm* t);  // "sábado, 26 de setembro"
const char* sysWeekdayShort(int wd);
const char* sysMonthName(int m);
time_t sysParseHttpDate(const char* s);
long sysDaysFromCivil(int y, int m, int d);

// usuario / hardware
const char* sysNick(void);
int sysFavColorIndex(void);
unsigned sysBattery(void);       // PM_BATT_LEVEL | PM_BATT_CHARGING
int  sysGetBacklight(void);      // -1 = indisponivel
void sysSetBacklight(int lvl);

// energia / sair
bool sysHasLoader(void);         // iniciado por um loader (TWiLight/nds-bootstrap)
void sysRequestExit(void);       // sai do app (volta ao loader)
bool sysExitRequested(void);
bool sysRebootTo(const char* sdPath);  // DSi + Unlaunch: reinicia abrindo o .nds
void sysShutdown(void);

u32 sysIpc(unsigned cmd, u32 arg);  // comando para o ARM7 (common/ipc.h)

// sons de interface
void sndClick(void);
void sndMove(void);
void sndBack(void);

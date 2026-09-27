// Rede: Wi-Fi, thread de trabalho e cliente HTTP(S)
#pragma once
#include <nds.h>
#include <calico/dev/wlan.h>

typedef enum NetState {
	NET_OFF = 0,
	NET_CONNECTING,
	NET_ONLINE,
	NET_NOCONFIG,   // nenhum perfil Wi-Fi salvo
	NET_FAILED,
} NetState;

void netInit(void);
void netTick(void);  // chamado todo frame na thread principal
NetState netState(void);
bool netBusy(void);  // thread de rede trabalhando
extern int g_tlsLastMs;
const char* netStateText(void);
int netBars(void);   // -1 = offline, 0..3
void netReconnect(void);
u32 netIP(void);
const char* netSsid(void);
bool netScanStart(void);
int  netScanPoll(WlanBssDesc** out);  // -1 = procurando, senao quantidade
bool netScanning(void);
bool netBssSecure(const WlanBssDesc* b);
bool netBssNeedsWpa(const WlanBssDesc* b);
bool netConnectTo(const WlanBssDesc* bss, const char* pass);
bool netConnectSsid(const char* ssid, const char* pass);
int  netConnectResult(char* err, int sz);  // 1 conectando, 0 ok, -1 erro, 2 nada
void netTrySaved(void);
void netIPInfo(u32* gw, u32* mask, u32* dns1, u32* dns2);

// jobs executados na thread de rede
typedef struct NetJob NetJob;
typedef void (*NetJobFn)(NetJob* j);
enum { JOB_IDLE = 0, JOB_QUEUED, JOB_RUNNING, JOB_DONE };
struct NetJob {
	NetJobFn run;
	void* user;
	volatile int state;
	volatile int progress;  // bytes recebidos
	volatile int total;     // bytes esperados (0 = desconhecido)
	volatile bool cancel;
	int result;
	char err[96];
};
bool netSubmit(NetJob* j);
static inline bool jobBusy(const NetJob* j) { return j->state == JOB_QUEUED || j->state == JOB_RUNNING; }
bool netWaitOnline(NetJob* j, int ms);

// HTTP
typedef struct HttpResp {
	int status;
	char* body;   // malloc, terminado em NUL
	int len, cap;
	bool truncated;
	char ctype[96];
	char url[1024];  // URL final (apos redirecionamentos)
} HttpResp;

typedef struct HttpReq {
	const char* method;       // NULL = GET
	const char* body;         // POST
	int bodyLen;
	const char* contentType;  // POST
	const char* extraHeaders; // "Nome: valor\r\n..."
	int maxBody;              // limite do corpo
	int timeoutMs;
} HttpReq;

int httpRequest(NetJob* j, const char* url, const HttpReq* rq, HttpResp* r);  // 0 = ok, <0 = erro (j->err)
int httpGet(NetJob* j, const char* url, HttpResp* r, int maxBody);
void httpFree(HttpResp* r);
bool urlParse(const char* url, bool* https, char* host, int hostSz, int* port, char* path, int pathSz);
void urlResolve(const char* base, const char* rel, char* out, int sz);
void urlEncode(const char* in, char* out, int sz);

// TLS (net_tls.c) — retorna false se indisponivel
bool tlsAvailable(void);

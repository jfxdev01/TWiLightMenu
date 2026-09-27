// DSi Dash — Wi-Fi, thread de rede, cliente HTTP
#include "common.h"
#include <dswifi9.h>
#include <wfc.h>
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include "net_conn.h"

static volatile bool s_inited;
static volatile int s_slots;
static volatile bool s_manual;       // conexao feita pelo app Wi-Fi (sem perfil no firmware)
static char s_ssid[36];
static volatile int s_scanState;     // 0 parado, 1 procurando, 2 pronto
static WlanBssDesc s_scan[WLAN_MAX_BSS_ENTRIES];
static int s_scanCount;
static NetState s_state = NET_CONNECTING;
static int s_offlineFrames;

static volatile bool s_running;
static Thread s_thr;
alignas(8) static u8 s_stack[48 * 1024];
static Mailbox s_mb;
static u32 s_mbSlots[8];

// ---------------------------------------------------------------------------
// thread de rede
// ---------------------------------------------------------------------------
static int workerMain(void* arg) {
	Wifi_InitDefault(INIT_ONLY);
	wfcLoadFromNvram();  // INIT_ONLY nao carrega os perfis salvos no console
	s_slots = wfcGetNumSlots();
	if (s_slots > 0) Wifi_AutoConnect();
	s_inited = true;
	if (s_slots == 0) netTrySaved();
	for (;;) {
		NetJob* j = (NetJob*)mailboxRecv(&s_mb);
		s_running = true;
		j->state = JOB_RUNNING;
		j->err[0] = 0;
		j->result = 0;
		j->run(j);
		s_running = false;
		j->state = JOB_DONE;
	}
	return 0;
}

void netInit(void) {
	mailboxPrepare(&s_mb, s_mbSlots, ARRAY_SIZE(s_mbSlots));
	threadPrepare(&s_thr, workerMain, NULL, &s_stack[sizeof(s_stack)], MAIN_THREAD_PRIO + 2);
	size_t tls = threadGetLocalStorageSize();
	if (tls) threadAttachLocalStorage(&s_thr, memalign(8, tls));
	threadStart(&s_thr);
}

bool netSubmit(NetJob* j) {
	if (jobBusy(j)) return false;
	j->state = JOB_QUEUED;
	j->cancel = false;
	j->progress = j->total = 0;
	if (!mailboxTrySend(&s_mb, (u32)j)) {
		j->state = JOB_IDLE;
		return false;
	}
	return true;
}

bool netWaitOnline(NetJob* j, int ms) {
	for (int t = 0; t < ms; t += 50) {
		if (s_inited && Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED) return true;
		if (j && j->cancel) return false;
		if (s_inited && s_slots == 0 && !s_manual) return false;
		threadSleep(50000);
	}
	return false;
}

void netTick(void) {
	NetState ns;
	if (!s_inited) ns = NET_CONNECTING;
	else if (s_slots == 0 && !s_manual) ns = NET_NOCONFIG;
	else {
		int st = Wifi_AssocStatus();
		if (st == ASSOCSTATUS_ASSOCIATED) {
			ns = NET_ONLINE;
			s_offlineFrames = 0;
		} else if (st == ASSOCSTATUS_DISCONNECTED) {
			// reconecta sozinho depois de alguns segundos
			if (++s_offlineFrames == 180 && !s_manual && !netScanning()) Wifi_AutoConnect();
			if (s_offlineFrames > 600) {
				s_offlineFrames = 0;
			}
			ns = s_offlineFrames < 180 ? NET_FAILED : NET_CONNECTING;
		} else {
			ns = NET_CONNECTING;
		}
	}
	if (ns != s_state) {
		s_state = ns;
		gfxInvalidate(GFX_BOTH);
	}
	static int lastBars = -2;
	if ((g_frame & 63) == 0) {
		int b = netBars();
		if (b != lastBars) {
			lastBars = b;
			gfxInvalidate(GFX_TOP);
		}
	}
}

NetState netState(void) { return s_state; }
bool netBusy(void) { return s_running; }

const char* netStateText(void) {
	switch (s_state) {
		case NET_OFF: return "Desligado";
		case NET_CONNECTING: return "Conectando\xE2\x80\xA6";
		case NET_ONLINE: return "Conectado";
		case NET_NOCONFIG: return "Sem rede configurada";
		case NET_FAILED: return "Sem conex\xC3\xA3o";
	}
	return "?";
}

int netBars(void) {
	if (s_state != NET_ONLINE) return -1;
	int b = (int)wlmgrGetSignalStrength();
	return CLAMP(b, 0, 3);
}

void netReconnect(void) {
	if (!s_inited) return;
	wfcLoadFromNvram();
	s_slots = wfcGetNumSlots();
	if (s_slots > 0) Wifi_AutoConnect();
	s_offlineFrames = 0;
}

// ---------------------------------------------------------------------------
// procurar / conectar manualmente (app Wi-Fi)
// ---------------------------------------------------------------------------
const char* netSsid(void) {
	if (s_state != NET_ONLINE) return "";
	if (s_manual) return s_ssid;
	WfcConnSlot* sl = wfcGetActiveSlot();
	if (sl) {
		int n = sl->ssid_len ? MIN(sl->ssid_len, 32) : (int)strnlen(sl->ssid, 32);
		memcpy(s_ssid, sl->ssid, n);
		s_ssid[n] = 0;
	}
	return s_ssid;
}

// filtro "qualquer rede" (passar NULL para wfcBeginScan quebra nesta versao do calico)
static const WlanBssScanFilter* anyFilter(void) {
	static WlanBssScanFilter f;
	memset(&f, 0, sizeof(f));
	f.channel_mask = 0x7FFF;  // canais 1..14 (cobre bit=canal e bit=canal-1)
	memset(f.target_bssid, 0xFF, 6);
	return &f;
}

static bool s_wasOnline;
static int s_scanWait;
static WlanBssDesc s_lastBss;
static WlanAuthData s_lastAuth;
static bool s_haveLast;

bool netScanStart(void) {
	if (!s_inited || s_scanState == 1 || s_scanState == 3) return false;
	s_scanCount = 0;
	s_scanWait = 0;
	s_wasOnline = Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED;
	if (wfcGetStatus() != WfcStatus_Disconnected) {
		Wifi_DisconnectAP();  // o hardware so procura redes desconectado
		s_scanState = 3;
		return true;
	}
	if (!wfcBeginScan(anyFilter())) return false;
	s_scanState = 1;
	s_scanWait = 0;
	return true;
}

static void reconnectPrevious(void) {
	if (s_manual && s_haveLast) {
		WlanBssDesc b = s_lastBss;
		wfcBeginConnect(&b, b.auth_type == WlanBssAuthType_Open ? NULL : &s_lastAuth);
	} else if (s_slots > 0) {
		Wifi_AutoConnect();
	}
}

int netScanPoll(WlanBssDesc** out) {
	*out = s_scan;
	if (s_scanState == 3) {  // esperando desconectar (driver precisa voltar a Idle)
		static int idleFrames;
		if (wfcGetStatus() == WfcStatus_Disconnected && wlmgrGetState() == WlMgrState_Idle) idleFrames++;
		else idleFrames = 0;
		if (idleFrames > 20) {
			idleFrames = 0;
			if (wfcBeginScan(anyFilter())) {
				s_scanState = 1;
				s_scanWait = 0;
			}
			else if (++s_scanWait > 120) {
				s_scanState = 2;
				if (s_wasOnline) reconnectPrevious();
			}
		} else if (++s_scanWait > 300) {
			s_scanState = 2;
		}
		return -1;
	}
	// o estado muda de forma assincrona: espera um pouco antes de aceitar "terminou"
	if (s_scanState == 1 && ++s_scanWait > 30 && (wfcGetStatus() != WfcStatus_Scanning || s_scanWait > 900)) {
		unsigned n = 0;
		WlanBssDesc* l = wfcGetScanBssList(&n);
		s_scanCount = 0;
		for (unsigned i = 0; l && i < n && s_scanCount < WLAN_MAX_BSS_ENTRIES; i++) {
			if (!l[i].ssid_len) continue;  // redes ocultas
			s_scan[s_scanCount++] = l[i];
		}
		// ordena por sinal
		for (int i = 0; i < s_scanCount; i++)
			for (int k = i + 1; k < s_scanCount; k++)
				if (s_scan[k].rssi > s_scan[i].rssi) {
					WlanBssDesc t = s_scan[i];
					s_scan[i] = s_scan[k];
					s_scan[k] = t;
				}
		s_scanState = 2;
		if (s_wasOnline) reconnectPrevious();
	}
	return (s_scanState == 1) ? -1 : s_scanCount;
}

bool netScanning(void) { return s_scanState == 1 || s_scanState == 3; }

bool netBssSecure(const WlanBssDesc* b) { return (b->auth_mask & ~1u) != 0; }
bool netBssNeedsWpa(const WlanBssDesc* b) { return (b->auth_mask & 0xF0) != 0; }

static WlanBssAuthType bestAuth(u8 mask) {
	for (int t = 7; t >= 0; t--) {
		if (!(mask & (1u << t))) continue;
		if (t >= 4 && !sysIsDSi()) continue;  // WPA so no modo DSi
		return (WlanBssAuthType)t;
	}
	return WlanBssAuthType_Open;
}

static struct {
	WlanBssDesc bss;
	char pass[68];
	bool save;
} s_conn;
static NetJob s_connJob;

static bool doConnect(WlanBssDesc* bss, const char* pass, WlanAuthData* authOut) {
	WlanAuthData auth;
	memset(&auth, 0, sizeof(auth));
	WlanBssAuthType at = bestAuth(bss->auth_mask);
	bss->auth_type = at;
	if (at >= WlanBssAuthType_WPA_PSK_TKIP) {
		if (!wfcDeriveWpaKey(&auth, bss->ssid, bss->ssid_len, pass, strlen(pass))) return false;
	} else if (at != WlanBssAuthType_Open) {
		int n = MIN((int)strlen(pass), WLAN_WEP_128_LEN);
		memcpy(auth.wep_key, pass, n);
	}
	if (authOut) *authOut = auth;
	s_lastBss = *bss;
	s_lastAuth = auth;
	s_haveLast = true;
	s_manual = true;
	int n = MIN(bss->ssid_len, 32);
	memcpy(s_ssid, bss->ssid, n);
	s_ssid[n] = 0;
	return wfcBeginConnect(bss, at == WlanBssAuthType_Open ? NULL : &auth);
}

static void saveNetwork(const WlanBssDesc* bss, const char* pass) {
	if (!sysHasStorage()) return;
	char p[96];
	sysDataPath(p, sizeof(p), "wifi.txt");
	FILE* f = fopen(p, "w");
	if (!f) return;
	fprintf(f, "%.*s\n%s\n", bss->ssid_len, bss->ssid, pass);
	fclose(f);
}

static void connJob(NetJob* j) {
	if (!doConnect(&s_conn.bss, s_conn.pass, NULL)) {
		snprintf(j->err, sizeof(j->err), "N\xC3\xA3o foi poss\xC3\xADvel iniciar a conex\xC3\xA3o");
		j->result = -1;
		return;
	}
	for (int t = 0; t < 20000; t += 100) {
		if (Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED) {
			if (s_conn.save) saveNetwork(&s_conn.bss, s_conn.pass);
			j->result = 0;
			return;
		}
		if (t > 3000 && wfcGetStatus() == WfcStatus_Disconnected) break;
		threadSleep(100000);
	}
	snprintf(j->err, sizeof(j->err), "Falha ao conectar (senha errada?)");
	j->result = -2;
}

bool netConnectTo(const WlanBssDesc* bss, const char* pass) {
	if (jobBusy(&s_connJob)) return false;
	s_conn.bss = *bss;
	snprintf(s_conn.pass, sizeof(s_conn.pass), "%s", pass ? pass : "");
	s_conn.save = true;
	s_connJob.run = connJob;
	return netSubmit(&s_connJob);
}

int netConnectResult(char* err, int sz) {
	if (s_connJob.state != JOB_DONE) return jobBusy(&s_connJob) ? 1 : 2;
	s_connJob.state = JOB_IDLE;
	if (err) snprintf(err, sz, "%s", s_connJob.err);
	return s_connJob.result == 0 ? 0 : -1;
}

// conecta por nome (QR code de Wi-Fi): procura a rede e conecta, salvando-a
static char s_qSsid[40], s_qPass[72];

static void ssidJob(NetJob* j) {
	for (int attempt = 0; attempt < 3; attempt++) {
		if (wfcGetStatus() != WfcStatus_Disconnected) {
			Wifi_DisconnectAP();
			for (int t = 0; t < 3000 && wfcGetStatus() != WfcStatus_Disconnected; t += 100) threadSleep(100000);
			threadSleep(300000);
		}
		if (!wfcBeginScan(anyFilter())) {
			threadSleep(500000);
			continue;
		}
		threadSleep(500000);
		for (int t = 0; t < 8000 && wfcGetStatus() == WfcStatus_Scanning; t += 100) threadSleep(100000);
		unsigned n = 0;
		WlanBssDesc* l = wfcGetScanBssList(&n);
		for (unsigned i = 0; l && i < n; i++) {
			if (l[i].ssid_len == strlen(s_qSsid) && !memcmp(l[i].ssid, s_qSsid, l[i].ssid_len)) {
				s_conn.bss = l[i];
				snprintf(s_conn.pass, sizeof(s_conn.pass), "%s", s_qPass);
				s_conn.save = true;
				connJob(j);
				return;
			}
		}
	}
	snprintf(j->err, sizeof(j->err), "Rede \"%.30s\" n\xC3\xA3o encontrada", s_qSsid);
	j->result = -1;
}

bool netConnectSsid(const char* ssid, const char* pass) {
	if (jobBusy(&s_connJob)) return false;
	snprintf(s_qSsid, sizeof(s_qSsid), "%s", ssid);
	snprintf(s_qPass, sizeof(s_qPass), "%s", pass ? pass : "");
	s_connJob.run = ssidJob;
	return netSubmit(&s_connJob);
}

// no boot, sem perfis no firmware: procura a rede salva pelo app Wi-Fi (roda na thread de rede)
void netTrySaved(void) {
	if (!sysHasStorage()) return;
	char p[96], ssid[40] = {0}, pass[72] = {0};
	sysDataPath(p, sizeof(p), "wifi.txt");
	FILE* f = fopen(p, "r");
	if (!f) return;
	if (!fgets(ssid, sizeof(ssid), f)) ssid[0] = 0;
	if (!fgets(pass, sizeof(pass), f)) pass[0] = 0;
	fclose(f);
	ssid[strcspn(ssid, "\r\n")] = 0;
	pass[strcspn(pass, "\r\n")] = 0;
	if (!ssid[0]) return;
	s_manual = true;
	for (int attempt = 0; attempt < 3; attempt++) {
		if (!wfcBeginScan(anyFilter())) return;
		for (int t = 0; t < 8000 && wfcGetStatus() == WfcStatus_Scanning; t += 100) threadSleep(100000);
		unsigned n = 0;
		WlanBssDesc* l = wfcGetScanBssList(&n);
		for (unsigned i = 0; l && i < n; i++) {
			if (l[i].ssid_len == strlen(ssid) && !memcmp(l[i].ssid, ssid, l[i].ssid_len)) {
				WlanBssDesc b = l[i];
				doConnect(&b, pass, NULL);
				return;
			}
		}
	}
	s_manual = false;
}

u32 netIP(void) { return s_state == NET_ONLINE ? Wifi_GetIP() : 0; }

void netIPInfo(u32* gw, u32* mask, u32* dns1, u32* dns2) {
	struct in_addr g, m, d1, d2;
	Wifi_GetIPInfo(&g, &m, &d1, &d2);
	*gw = g.s_addr;
	*mask = m.s_addr;
	*dns1 = d1.s_addr;
	*dns2 = d2.s_addr;
}

// ---------------------------------------------------------------------------
// conexao (TCP puro ou TLS)
// ---------------------------------------------------------------------------
static int waitStep(Conn* c, int* waited) {
	if (c->job && c->job->cancel) return -2;
	if (*waited >= c->timeoutMs) return -3;
	threadSleep(8000);
	*waited += 8;
	return 0;
}

int sockReadRaw(Conn* c, void* buf, int len) {
	int waited = 0;
	for (;;) {
		int r = recv(c->sock, buf, len, 0);
		if (r > 0) return r;
		if (r == 0) return 0;
		if (errno != EWOULDBLOCK && errno != EAGAIN) return -1;
		int w = waitStep(c, &waited);
		if (w) return w;
	}
}

int sockWriteRaw(Conn* c, const void* buf, int len) {
	const u8* p = (const u8*)buf;
	int left = len, waited = 0;
	while (left > 0) {
		int r = send(c->sock, p, left, 0);
		if (r > 0) {
			p += r;
			left -= r;
			waited = 0;
			continue;
		}
		if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) return -1;
		int w = waitStep(c, &waited);
		if (w) return w;
	}
	return len;
}

static int connOpen(Conn* c, NetJob* j, const char* host, int port, bool https, int timeoutMs) {
	memset(c, 0, sizeof(*c));
	c->sock = -1;
	c->job = j;
	c->timeoutMs = timeoutMs;
	if (https && !tlsAvailable()) {
		snprintf(j->err, sizeof(j->err), "HTTPS indispon\xC3\xADvel");
		return -10;
	}
	if (!netWaitOnline(j, 15000)) {
		snprintf(j->err, sizeof(j->err), j->cancel ? "Cancelado" : "Sem conex\xC3\xA3o Wi-Fi");
		return -1;
	}
	struct hostent* h = gethostbyname(host);
	if (!h || !h->h_addr_list[0]) {
		snprintf(j->err, sizeof(j->err), "Endere\xC3\xA7o n\xC3\xA3o encontrado: %.40s", host);
		return -2;
	}
	c->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (c->sock < 0) {
		snprintf(j->err, sizeof(j->err), "Erro de socket");
		return -3;
	}
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	memcpy(&sa.sin_addr.s_addr, h->h_addr_list[0], 4);
	// connect nao-bloqueante com limite de tempo (um servidor mudo nao trava a thread de rede)
	int one = 1;
	ioctl(c->sock, FIONBIO, &one);
	int cr = connect(c->sock, (struct sockaddr*)&sa, sizeof(sa));
	if (cr < 0 && errno != EINPROGRESS && errno != EWOULDBLOCK) {
		closesocket(c->sock);
		c->sock = -1;
		snprintf(j->err, sizeof(j->err), "Falha ao conectar em %.40s", host);
		return -4;
	}
	for (int waited = 0;; waited += 10) {
		struct sockaddr_in pa;
		socklen_t pl = sizeof(pa);
		if (getpeername(c->sock, (struct sockaddr*)&pa, &pl) == 0) break;  // ESTABLISHED
		if (j->cancel || waited >= 12000) {
			closesocket(c->sock);
			c->sock = -1;
			snprintf(j->err, sizeof(j->err), j->cancel ? "Cancelado" : "Tempo esgotado ao conectar em %.40s", host);
			return -4;
		}
		threadSleep(10000);
	}
	if (https) {
		int r = tlsOpen(c, host);
		if (r < 0) {
			if (!j->err[0]) snprintf(j->err, sizeof(j->err), "Falha TLS (%d)", r);
			closesocket(c->sock);
			c->sock = -1;
			return -5;
		}
	}
	return 0;
}

static int connWrite(Conn* c, const void* d, int n) { return c->tls ? tlsWrite(c, d, n) : sockWriteRaw(c, d, n); }
static int connRead(Conn* c, void* d, int n) { return c->tls ? tlsRead(c, d, n) : sockReadRaw(c, d, n); }

static void connClose(Conn* c) {
	if (c->tls) tlsClose(c);
	if (c->sock >= 0) {
		shutdown(c->sock, SHUT_RDWR);
		closesocket(c->sock);
	}
	c->sock = -1;
}

// leitor com buffer
typedef struct Reader {
	Conn* c;
	int pos, len;
	bool eof;
	char buf[4096];
} Reader;

static int rdFill(Reader* r) {
	if (r->eof) return 0;
	int n = connRead(r->c, r->buf, sizeof(r->buf));
	if (n <= 0) {
		r->eof = true;
		return n;
	}
	r->pos = 0;
	r->len = n;
	if (r->c->job) r->c->job->progress += n;
	return n;
}

static int rdLine(Reader* r, char* out, int sz) {
	int n = 0;
	for (;;) {
		if (r->pos >= r->len) {
			int f = rdFill(r);
			if (f <= 0) return n ? n : (f < 0 ? f : -1);
		}
		char ch = r->buf[r->pos++];
		if (ch == '\n') break;
		if (ch != '\r' && n < sz - 1) out[n++] = ch;
	}
	out[n] = 0;
	return n;
}

static int rdSome(Reader* r, char* out, int max) {
	if (r->pos >= r->len) {
		int f = rdFill(r);
		if (f <= 0) return f;
	}
	int n = MIN(max, r->len - r->pos);
	memcpy(out, r->buf + r->pos, n);
	r->pos += n;
	return n;
}

static bool bodyAppend(HttpResp* h, const char* d, int n, int maxBody) {
	if (h->len + n > maxBody) {
		n = maxBody - h->len;
		h->truncated = true;
	}
	if (n > 0) {
		if (h->len + n + 1 > h->cap) {
			int nc = MAX(h->cap * 2, 16384);
			while (nc < h->len + n + 1) nc *= 2;
			if (nc > maxBody + 1) nc = maxBody + 1;
			char* nb = (char*)realloc(h->body, nc);
			if (!nb) {
				h->truncated = true;
				return false;
			}
			h->body = nb;
			h->cap = nc;
		}
		memcpy(h->body + h->len, d, n);
		h->len += n;
		h->body[h->len] = 0;
	}
	return !h->truncated;
}

void httpFree(HttpResp* r) {
	free(r->body);
	r->body = NULL;
	r->len = r->cap = 0;
}

int httpGet(NetJob* j, const char* url, HttpResp* r, int maxBody) {
	HttpReq rq = {0};
	rq.maxBody = maxBody;
	return httpRequest(j, url, &rq, r);
}

static bool hdrIs(const char* line, const char* name, const char** val) {
	int n = strlen(name);
	if (strncasecmp(line, name, n) || line[n] != ':') return false;
	const char* v = line + n + 1;
	while (*v == ' ' || *v == '\t') v++;
	*val = v;
	return true;
}

int httpRequest(NetJob* j, const char* url0, const HttpReq* rq, HttpResp* r) {
	memset(r, 0, sizeof(*r));
	int maxBody = rq->maxBody > 0 ? rq->maxBody : (sysIsDSi() ? 2 * 1024 * 1024 : 384 * 1024);
	int timeout = rq->timeoutMs > 0 ? rq->timeoutMs : 20000;
	char* url = (char*)malloc(1024);
	char* path = (char*)malloc(1024);
	char* line = (char*)malloc(2048);
	Reader* rd = (Reader*)malloc(sizeof(Reader));
	char host[128];
	int ret = -1;
	if (!url || !path || !line || !rd) {
		snprintf(j->err, sizeof(j->err), "Sem mem\xC3\xB3ria");
		goto out;
	}
	snprintf(url, 1024, "%s", url0);
	const char* method = rq->method ? rq->method : "GET";
	HttpReq getRq = {.maxBody = maxBody, .timeoutMs = timeout};

	for (int redir = 0; redir < 8; redir++) {
		bool https;
		int port;
		if (!urlParse(url, &https, host, sizeof(host), &port, path, 1024)) {
			snprintf(j->err, sizeof(j->err), "URL inv\xC3\xA1lida");
			ret = -20;
			goto out;
		}
		snprintf(r->url, sizeof(r->url), "%s", url);
		Conn c;
		ret = connOpen(&c, j, host, port, https, timeout);
		if (ret < 0) goto out;

		bool defPort = (https && port == 443) || (!https && port == 80);
		char hostHdr[160];
		if (defPort) snprintf(hostHdr, sizeof(hostHdr), "%s", host);
		else snprintf(hostHdr, sizeof(hostHdr), "%s:%d", host, port);
		int n = snprintf(line, 2048,
			"%s %s HTTP/1.1\r\nHost: %s\r\n"
			"User-Agent: Mozilla/5.0 (Nintendo DSi; U; pt-BR) DSiDash/" APP_VERSION " Mobile\r\n"
			"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,text/plain;q=0.8,*/*;q=0.5\r\n"
			"Accept-Language: pt-BR,pt;q=0.9,en;q=0.6\r\n"
			"Accept-Encoding: identity\r\nConnection: close\r\n%s",
			method, path, hostHdr, rq->extraHeaders ? rq->extraHeaders : "");
		if (rq->body) n += snprintf(line + n, 2048 - n, "Content-Type: %s\r\nContent-Length: %d\r\n", rq->contentType ? rq->contentType : "application/x-www-form-urlencoded", rq->bodyLen);
		n += snprintf(line + n, 2048 - n, "\r\n");
		if (connWrite(&c, line, n) < 0 || (rq->body && connWrite(&c, rq->body, rq->bodyLen) < 0)) {
			snprintf(j->err, sizeof(j->err), "Falha ao enviar pedido");
			connClose(&c);
			ret = -6;
			goto out;
		}

		memset(rd, 0, sizeof(*rd));
		rd->c = &c;
		// linha de status
		if (rdLine(rd, line, 2048) <= 0 || strncmp(line, "HTTP/", 5)) {
			snprintf(j->err, sizeof(j->err), j->cancel ? "Cancelado" : "Resposta inv\xC3\xA1lida do servidor");
			connClose(&c);
			ret = -7;
			goto out;
		}
		const char* sp = strchr(line, ' ');
		r->status = sp ? atoi(sp + 1) : 0;
		int clen = -1;
		long age = 0;
		char dateHdr[48] = "";
		bool chunked = false;
		char location[1024] = {0};
		for (;;) {
			int ln = rdLine(rd, line, 2048);
			if (ln < 0) break;
			if (ln == 0) break;
			const char* v;
			if (hdrIs(line, "Content-Length", &v)) clen = atoi(v);
			else if (hdrIs(line, "Transfer-Encoding", &v)) chunked = strstr(v, "chunked") != NULL;
			else if (hdrIs(line, "Content-Type", &v)) snprintf(r->ctype, sizeof(r->ctype), "%s", v);
			else if (hdrIs(line, "Location", &v)) snprintf(location, sizeof(location), "%s", v);
			else if (hdrIs(line, "Date", &v)) snprintf(dateHdr, sizeof(dateHdr), "%s", v);
			else if (hdrIs(line, "Age", &v)) age = atol(v);
		}
		// respostas de cache (CDN) trazem o Date original + Age
		if (dateHdr[0]) sysOnHttpDate(dateHdr, age);
		if (r->status >= 300 && r->status < 400 && location[0] && r->status != 304) {
			connClose(&c);
			char* nu = (char*)malloc(1024);
			if (!nu) goto out;
			urlResolve(url, location, nu, 1024);
			snprintf(url, 1024, "%s", nu);
			free(nu);
			if (r->status == 303 || ((r->status == 301 || r->status == 302) && strcmp(method, "GET"))) {
				method = "GET";
				rq = &getRq;
			}
			continue;
		}
		j->total = clen > 0 ? clen : 0;
		j->progress = 0;

		// corpo
		char* tmp = line;
		if (chunked) {
			for (;;) {
				if (rdLine(rd, tmp, 64) < 0) break;
				int csz = (int)strtol(tmp, NULL, 16);
				if (csz <= 0) break;
				while (csz > 0) {
					int k = rdSome(rd, tmp, MIN(csz, 2048));
					if (k <= 0) break;
					csz -= k;
					if (!bodyAppend(r, tmp, k, maxBody)) break;
				}
				if (csz > 0 || r->truncated) break;
				rdLine(rd, tmp, 8);  // CRLF apos o bloco
			}
		} else {
			int left = clen;
			while (left != 0) {
				int k = rdSome(rd, tmp, left > 0 ? MIN(left, 2048) : 2048);
				if (k <= 0) break;
				if (left > 0) left -= k;
				if (!bodyAppend(r, tmp, k, maxBody)) break;
			}
		}
		connClose(&c);
		if (!r->body) {
			r->body = (char*)calloc(1, 1);
			r->len = 0;
		}
		if (j->cancel) {
			snprintf(j->err, sizeof(j->err), "Cancelado");
			ret = -8;
			goto out;
		}
		ret = 0;
		goto out;
	}
	snprintf(j->err, sizeof(j->err), "Redirecionamentos demais");
	ret = -9;
out:
	free(url);
	free(path);
	free(line);
	free(rd);
	if (ret < 0) httpFree(r);
	return ret;
}

// ---------------------------------------------------------------------------
// URLs
// ---------------------------------------------------------------------------
bool urlParse(const char* url, bool* https, char* host, int hostSz, int* port, char* path, int pathSz) {
	const char* p = url;
	if (!strncasecmp(p, "http://", 7)) {
		*https = false;
		p += 7;
	} else if (!strncasecmp(p, "https://", 8)) {
		*https = true;
		p += 8;
	} else {
		return false;
	}
	*port = *https ? 443 : 80;
	const char* hs = p;
	while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#') p++;
	int hl = (int)(p - hs);
	if (hl <= 0 || hl >= hostSz) return false;
	memcpy(host, hs, hl);
	host[hl] = 0;
	// usuario@host nao suportado; remove
	char* at = strchr(host, '@');
	if (at) memmove(host, at + 1, strlen(at));
	if (*p == ':') {
		*port = atoi(p + 1);
		while (*p && *p != '/' && *p != '?' && *p != '#') p++;
	}
	if (*p == '/' || *p == '?') {
		int n = 0;
		if (*p == '?') path[n++] = '/';
		while (*p && *p != '#' && n < pathSz - 1) {
			unsigned char ch = (unsigned char)*p++;
			if (ch <= ' ' || ch >= 0x7F) {  // escapa espacos / nao-ASCII
				if (n + 3 >= pathSz) break;
				static const char* hx = "0123456789ABCDEF";
				path[n++] = '%';
				path[n++] = hx[ch >> 4];
				path[n++] = hx[ch & 15];
			} else {
				path[n++] = ch;
			}
		}
		path[n] = 0;
	} else {
		snprintf(path, pathSz, "/");
	}
	return true;
}

static void normalizePath(char* p) {
	// p comeca com '/'; resolve "." e ".." (sem mexer na query)
	char* q = strchr(p, '?');
	char query[512] = {0};
	if (q) {
		snprintf(query, sizeof(query), "%s", q);
		*q = 0;
	}
	char* segs[64];
	int n = 0;
	bool trailing = strlen(p) > 1 && p[strlen(p) - 1] == '/';
	char* save = NULL;
	for (char* s = strtok_r(p + 1, "/", &save); s; s = strtok_r(NULL, "/", &save)) {
		if (!strcmp(s, ".")) continue;
		if (!strcmp(s, "..")) {
			if (n > 0) n--;
			continue;
		}
		if (n < 64) segs[n++] = s;
	}
	char out[1024];
	int o = 0;
	out[o++] = '/';
	for (int i = 0; i < n; i++) {
		int l = strlen(segs[i]);
		if (o + l + 2 >= (int)sizeof(out)) break;
		memcpy(out + o, segs[i], l);
		o += l;
		if (i < n - 1 || trailing) out[o++] = '/';
	}
	out[o] = 0;
	strcpy(p, out);
	strcat(p, query);
}

void urlResolve(const char* base, const char* rel, char* out, int sz) {
	while (*rel == ' ' || *rel == '\t' || *rel == '\n' || *rel == '\r') rel++;
	// ja tem esquema?
	const char* c = rel;
	while (*c && (isalnum((unsigned char)*c) || *c == '+' || *c == '-' || *c == '.')) c++;
	if (*c == ':' && c > rel) {
		snprintf(out, sz, "%s", rel);
		return;
	}
	bool https;
	char host[128], path[1024];
	int port;
	if (!urlParse(base, &https, host, sizeof(host), &port, path, sizeof(path))) {
		snprintf(out, sz, "%s", rel);
		return;
	}
	char origin[160];
	bool defPort = (https && port == 443) || (!https && port == 80);
	if (defPort) snprintf(origin, sizeof(origin), "%s://%s", https ? "https" : "http", host);
	else snprintf(origin, sizeof(origin), "%s://%s:%d", https ? "https" : "http", host, port);

	if (rel[0] == '/' && rel[1] == '/') {
		snprintf(out, sz, "%s:%s", https ? "https" : "http", rel);
		return;
	}
	if (!rel[0] || rel[0] == '#') {
		snprintf(out, sz, "%s%s", origin, path);
		return;
	}
	char* np = (char*)malloc(2048);
	if (!np) {
		snprintf(out, sz, "%s", rel);
		return;
	}
	if (rel[0] == '/') {
		snprintf(np, 2048, "%s", rel);
	} else if (rel[0] == '?') {
		char* q = strchr(path, '?');
		if (q) *q = 0;
		snprintf(np, 2048, "%s%s", path, rel);
	} else {
		char* q = strchr(path, '?');
		if (q) *q = 0;
		char* slash = strrchr(path, '/');
		if (slash) slash[1] = 0;
		snprintf(np, 2048, "%s%s", path, rel);
	}
	char* frag = strchr(np, '#');
	if (frag) *frag = 0;
	normalizePath(np);
	snprintf(out, sz, "%s%s", origin, np);
	free(np);
}

void urlEncode(const char* in, char* out, int sz) {
	static const char* hx = "0123456789ABCDEF";
	int n = 0;
	for (; *in && n < sz - 4; in++) {
		unsigned char ch = (unsigned char)*in;
		if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') out[n++] = ch;
		else if (ch == ' ') out[n++] = '+';
		else {
			out[n++] = '%';
			out[n++] = hx[ch >> 4];
			out[n++] = hx[ch & 15];
		}
	}
	out[n] = 0;
}

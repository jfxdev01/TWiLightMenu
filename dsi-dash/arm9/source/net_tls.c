// DSi Dash — HTTPS com BearSSL (TLS 1.2) e as CAs raiz da Mozilla
#include "common.h"
#include "net_conn.h"
#include "bearssl.h"

extern const br_x509_trust_anchor g_trustAnchors[];
extern const unsigned g_trustAnchorsNum;

typedef struct TlsCtx {
	br_ssl_client_context sc;
	br_x509_minimal_context xc;
	br_sslio_context ioc;
	unsigned char buf[BR_SSL_BUFSIZE_BIDI];
} TlsCtx;

// cache de sessoes (retomada rapida: evita refazer a troca de chaves)
typedef struct SessEnt {
	char host[64];
	br_ssl_session_parameters p;
	bool valid;
} SessEnt;
static SessEnt s_sess[6];
static int s_sessNext;

// ChaCha20 primeiro: sem AES em hardware, e a cifra mais rapida no ARM9
static const uint16_t SUITES[] = {
	BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
	BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
	BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
	BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
	BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
	BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
	BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,
	BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
	BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
	BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
	BR_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
	BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
	BR_TLS_RSA_WITH_AES_128_GCM_SHA256,
	BR_TLS_RSA_WITH_AES_256_GCM_SHA384,
	BR_TLS_RSA_WITH_AES_128_CBC_SHA256,
	BR_TLS_RSA_WITH_AES_128_CBC_SHA,
	BR_TLS_RSA_WITH_AES_256_CBC_SHA,
};

int g_tlsLastMs;

bool tlsAvailable(void) { return true; }

static int lowRead(void* ctx, unsigned char* buf, size_t len) {
	Conn* c = (Conn*)ctx;
	int r = sockReadRaw(c, buf, len);
	if (r > 0) return r;
	if (r == 0) c->eof = true;
	else c->lowErr = r;
	return -1;
}

static int lowWrite(void* ctx, const unsigned char* buf, size_t len) {
	Conn* c = (Conn*)ctx;
	int r = sockWriteRaw(c, buf, len);
	if (r > 0) return r;
	c->lowErr = r ? r : -1;
	return -1;
}

static const char* errText(int e, int alert) {
	switch (e) {
		case BR_ERR_X509_EXPIRED: return "certificado vencido ou rel\xC3\xB3gio errado";
		case BR_ERR_X509_NOT_TRUSTED: return "certificado n\xC3\xA3o confi\xC3\xA1vel";
		case BR_ERR_X509_BAD_SERVER_NAME: return "certificado n\xC3\xA3o \xC3\xA9 deste site";
		case BR_ERR_X509_UNSUPPORTED: return "certificado n\xC3\xA3o suportado";
		case BR_ERR_BAD_VERSION:
		case BR_ERR_UNSUPPORTED_VERSION: return "o site exige TLS 1.3 (n\xC3\xA3o suportado)";
		case BR_ERR_BAD_CIPHER_SUITE: return "nenhuma cifra em comum";
		case BR_ERR_IO: return "conex\xC3\xA3o interrompida";
		case BR_ERR_RECV_FATAL_ALERT:
			if (alert == 70) return "o site exige TLS 1.3 (n\xC3\xA3o suportado)";
			if (alert == 40) return "o site recusou a conex\xC3\xA3o segura";
			return "o site encerrou a conex\xC3\xA3o segura";
	}
	return NULL;
}

// garante a hora da rede (validade dos certificados) antes do 1o HTTPS
static void ensureNetTime(Conn* c) {
	if (sysHaveNetTime() || !c->job) return;
	HttpResp r;
	if (httpGet(c->job, "http://api.open-meteo.com/v1/forecast?latitude=0&longitude=0", &r, 2048) == 0) httpFree(&r);
	c->job->err[0] = 0;
}

int tlsOpen(Conn* c, const char* host) {
	ensureNetTime(c);
	TlsCtx* t = (TlsCtx*)malloc(sizeof(TlsCtx));
	if (!t) {
		if (c->job) snprintf(c->job->err, sizeof(c->job->err), "Sem mem\xC3\xB3ria para HTTPS");
		return -1;
	}
	br_ssl_client_init_full(&t->sc, &t->xc, g_trustAnchors, g_trustAnchorsNum);
	br_ssl_engine_set_suites(&t->sc.eng, SUITES, ARRAY_SIZE(SUITES));
	br_ssl_engine_set_versions(&t->sc.eng, BR_TLS10, BR_TLS12);

	time_t utc = sysUtcNow();
	br_x509_minimal_set_time(&t->xc, (u32)(utc / 86400) + 719528, (u32)(utc % 86400));

	u8 seed[32];
	sysEntropyGet(seed);
	br_ssl_engine_inject_entropy(&t->sc.eng, seed, sizeof(seed));
	br_ssl_engine_set_buffer(&t->sc.eng, t->buf, sizeof(t->buf), 1);

	SessEnt* se = NULL;
	for (int i = 0; i < ARRAY_SIZE(s_sess); i++)
		if (s_sess[i].valid && !strcmp(s_sess[i].host, host)) se = &s_sess[i];
	if (se) br_ssl_engine_set_session_parameters(&t->sc.eng, &se->p);
	if (!br_ssl_client_reset(&t->sc, host, se ? 1 : 0)) {
		free(t);
		return -2;
	}
	br_sslio_init(&t->ioc, &t->sc.eng, lowRead, c, lowWrite, c);
	c->tls = t;
	c->eof = false;
	c->lowErr = 0;
	// forca o handshake
	u64 t0 = tickGetCount();
	int fr = br_sslio_flush(&t->ioc);
	g_tlsLastMs = (int)((tickGetCount() - t0) * 1000 / TICK_FREQ);
	if (fr < 0) {
		int e = br_ssl_engine_last_error(&t->sc.eng);
		int alert = (e >= BR_ERR_RECV_FATAL_ALERT && e < BR_ERR_SEND_FATAL_ALERT) ? e - BR_ERR_RECV_FATAL_ALERT : -1;
		if (alert >= 0) e = BR_ERR_RECV_FATAL_ALERT;
		const char* m = errText(e, alert);
		if (c->job) {
			if (c->lowErr == -2) snprintf(c->job->err, sizeof(c->job->err), "Cancelado");
			else if (c->lowErr == -3) snprintf(c->job->err, sizeof(c->job->err), "Tempo esgotado (HTTPS)");
			else if (m) snprintf(c->job->err, sizeof(c->job->err), "HTTPS: %s", m);
			else snprintf(c->job->err, sizeof(c->job->err), "HTTPS: erro %d", e);
		}
		if (se) se->valid = false;
		free(t);
		c->tls = NULL;
		return -3;
	}
	if (!se) {
		se = &s_sess[s_sessNext];
		s_sessNext = (s_sessNext + 1) % ARRAY_SIZE(s_sess);
		snprintf(se->host, sizeof(se->host), "%s", host);
	}
	br_ssl_engine_get_session_parameters(&t->sc.eng, &se->p);
	se->valid = true;
	return 0;
}

int tlsRead(Conn* c, void* buf, int len) {
	TlsCtx* t = (TlsCtx*)c->tls;
	int r = br_sslio_read(&t->ioc, buf, len);
	if (r > 0) return r;
	if (c->lowErr) return c->lowErr;  // cancelado / tempo esgotado
	int e = br_ssl_engine_last_error(&t->sc.eng);
	if (e == BR_ERR_OK || e == BR_ERR_IO || c->eof) return 0;  // muitos servidores fecham sem close_notify
	return -1;
}

int tlsWrite(Conn* c, const void* buf, int len) {
	TlsCtx* t = (TlsCtx*)c->tls;
	if (br_sslio_write_all(&t->ioc, buf, len) < 0) return -1;
	if (br_sslio_flush(&t->ioc) < 0) return -1;
	return len;
}

void tlsClose(Conn* c) {
	free(c->tls);
	c->tls = NULL;
}

// DSi Dash — servidor HTTP para transferir arquivos do/para o cartao SD pelo navegador do PC/celular
#include "common.h"
#include "srv.h"
#include "net_conn.h"
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <malloc.h>

static Thread s_thr;
alignas(8) static u8 s_stack[24 * 1024];
static volatile bool s_run, s_alive;
static int s_port;
static SrvStatus s_st;

const SrvStatus* srvStatus(void) { return &s_st; }
bool srvRunning(void) { return s_alive; }
int srvPort(void) { return s_port; }

static void logEvent(const char* fmt, const char* a) {
	for (int i = SRV_LOG - 1; i > 0; i--) memcpy(s_st.log[i], s_st.log[i - 1], sizeof(s_st.log[0]));
	snprintf(s_st.log[0], sizeof(s_st.log[0]), fmt, a);
	if (s_st.logN < SRV_LOG) s_st.logN++;
	s_st.version++;
}

// ---------------------------------------------------------------------------
// utilitarios
// ---------------------------------------------------------------------------
static int sendAll(Conn* c, const void* d, int n) { return sockWriteRaw(c, d, n); }

static int sendStr(Conn* c, const char* s) { return sendAll(c, s, strlen(s)); }

static void urlDecode(char* s) {
	char* o = s;
	for (; *s; s++) {
		if (*s == '+') *o++ = ' ';
		else if (*s == '%' && isxdigit((u8)s[1]) && isxdigit((u8)s[2])) {
			char h[3] = {s[1], s[2], 0};
			*o++ = (char)strtol(h, NULL, 16);
			s += 2;
		} else *o++ = *s;
	}
	*o = 0;
}

static bool queryParam(const char* q, const char* name, char* out, int sz) {
	out[0] = 0;
	if (!q) return false;
	int nl = strlen(name);
	const char* p = q;
	while (p && *p) {
		if (!strncmp(p, name, nl) && p[nl] == '=') {
			p += nl + 1;
			int n = 0;
			while (p[n] && p[n] != '&' && n < sz - 1) n++;
			memcpy(out, p, n);
			out[n] = 0;
			urlDecode(out);
			return true;
		}
		p = strchr(p, '&');
		if (p) p++;
	}
	return false;
}

// caminho relativo seguro -> caminho absoluto no cartao
static bool safePath(const char* rel, char* out, int sz) {
	if (strstr(rel, "..") || strchr(rel, '\\') || strchr(rel, ':')) return false;
	while (*rel == '/') rel++;
	snprintf(out, sz, "%s%s", sysRoot(), rel);
	return true;
}

static void htmlEsc(char* o, int sz, const char* s) {
	int n = 0;
	for (; *s && n < sz - 7; s++) {
		if (*s == '<') n += snprintf(o + n, sz - n, "&lt;");
		else if (*s == '>') n += snprintf(o + n, sz - n, "&gt;");
		else if (*s == '&') n += snprintf(o + n, sz - n, "&amp;");
		else if (*s == '"') n += snprintf(o + n, sz - n, "&quot;");
		else o[n++] = *s;
	}
	o[n] = 0;
}

static void urlEnc(const char* in, char* out, int sz) {
	static const char* hx = "0123456789ABCDEF";
	int n = 0;
	for (; *in && n < sz - 4; in++) {
		u8 ch = *in;
		if (isalnum(ch) || strchr("-_.~/", ch)) out[n++] = ch;
		else {
			out[n++] = '%';
			out[n++] = hx[ch >> 4];
			out[n++] = hx[ch & 15];
		}
	}
	out[n] = 0;
}

static void redirect(Conn* c, const char* dir, const char* msg) {
	char d[400], m[200], h[800];
	urlEnc(dir, d, sizeof(d));
	urlEnc(msg ? msg : "", m, sizeof(m));
	snprintf(h, sizeof(h), "HTTP/1.1 303 See Other\r\nLocation: /?d=%s&m=%s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", d, m);
	sendStr(c, h);
}

static void simple(Conn* c, int code, const char* text) {
	char h[256];
	snprintf(h, sizeof(h), "HTTP/1.1 %d X\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", code, (int)strlen(text));
	sendStr(c, h);
	sendStr(c, text);
}

// ---------------------------------------------------------------------------
// pagina com a listagem
// ---------------------------------------------------------------------------
static const char* CSS =
	"<style>body{font-family:system-ui,sans-serif;background:#ebebeb;color:#2d2d2d;margin:0}"
	"header{background:#fff;padding:14px 20px;box-shadow:0 1px 4px #0002;display:flex;align-items:center;gap:12px}"
	"header b{font-size:20px}.dot{width:14px;height:14px;border-radius:50%;background:#00c3e3}"
	"main{max-width:900px;margin:16px auto;padding:0 12px}.card{background:#fff;border-radius:14px;padding:14px 18px;margin-bottom:14px;box-shadow:0 1px 3px #0001}"
	"table{width:100%;border-collapse:collapse}td{padding:8px 6px;border-bottom:1px solid #eee}td.s{color:#888;text-align:right;white-space:nowrap}"
	"a{color:#0a7fa8;text-decoration:none}a:hover{text-decoration:underline}.del{color:#e5484d;font-size:13px}"
	"button,input[type=submit]{background:#00b4dc;color:#fff;border:0;border-radius:18px;padding:8px 18px;font-size:15px;cursor:pointer}"
	"input[type=text]{padding:7px 10px;border:1px solid #ccc;border-radius:10px}.msg{background:#e6f9fd;border-left:4px solid #00b4dc}"
	"#bar{height:6px;background:#00b4dc;width:0;border-radius:3px;transition:width .2s}</style>";

static void listing(Conn* c, const char* rel, const char* msg) {
	char path[400];
	if (!safePath(rel, path, sizeof(path))) {
		simple(c, 400, "caminho invalido");
		return;
	}
	DIR* d = opendir(path);
	if (!d) {
		simple(c, 404, "pasta nao encontrada");
		return;
	}
	sendStr(c, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
	char* b = (char*)malloc(4096);
	if (!b) {
		closedir(d);
		return;
	}
	char er[400], eu[600];
	htmlEsc(er, sizeof(er), rel[0] ? rel : "/");
	urlEnc(rel, eu, sizeof(eu));
	snprintf(b, 4096,
		"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
		"<title>DSi Dash</title>%s</head><body><header><div class=dot></div><b>DSi Dash</b> <span>Cart\xC3\xA3o SD do DSi</span></header><main>",
		CSS);
	sendStr(c, b);
	if (msg && msg[0]) {
		char em[300];
		htmlEsc(em, sizeof(em), msg);
		snprintf(b, 4096, "<div class='card msg'>%s</div>", em);
		sendStr(c, b);
	}
	// envio (upload) com barra de progresso via XHR (funciona tambem sem JS)
	snprintf(b, 4096,
		"<div class=card><b>Enviar arquivos para <code>%s</code></b><form id=up method=post enctype=multipart/form-data action='/up?d=%s'>"
		"<p><input type=file name=f multiple> <button>Enviar</button></p><div id=bar></div></form>"
		"<form method=get action=/mk><input type=hidden name=d value='%s'><input type=text name=n placeholder='nova pasta'> <input type=submit value='Criar pasta'></form></div>"
		"<script>document.getElementById('up').onsubmit=function(e){e.preventDefault();var x=new XMLHttpRequest();"
		"x.upload.onprogress=function(p){document.getElementById('bar').style.width=(100*p.loaded/p.total)+'%%'};"
		"x.onload=function(){location.href=x.responseURL||location.href};x.open('POST',this.action);x.send(new FormData(this));}</script>",
		er, eu, er);
	sendStr(c, b);
	// migalhas
	sendStr(c, "<div class=card><p><a href='/?d='>\xF0\x9F\x93\x81 SD</a>");
	char acc[400] = "";
	const char* p = rel;
	while (*p) {
		const char* sl = strchr(p, '/');
		int n = sl ? (int)(sl - p) : (int)strlen(p);
		if (n > 0) {
			char seg[128], es[256], ea[600];
			snprintf(seg, sizeof(seg), "%.*s", n, p);
			int al = strlen(acc);
			snprintf(acc + al, sizeof(acc) - al, "%s%s", al ? "/" : "", seg);
			htmlEsc(es, sizeof(es), seg);
			urlEnc(acc, ea, sizeof(ea));
			snprintf(b, 4096, " / <a href='/?d=%s'>%s</a>", ea, es);
			sendStr(c, b);
		}
		p += n;
		if (*p == '/') p++;
	}
	sendStr(c, "</p><table>");
	struct dirent* e;
	int count = 0;
	// pastas primeiro, depois arquivos
	for (int pass = 0; pass < 2; pass++) {
		rewinddir(d);
		while ((e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			bool isDir = e->d_type == DT_DIR;
			if (isDir != (pass == 0)) continue;
			char relp[512], en[300], eu2[800];
			snprintf(relp, sizeof(relp), "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
			htmlEsc(en, sizeof(en), e->d_name);
			urlEnc(relp, eu2, sizeof(eu2));
			if (isDir) {
				snprintf(b, 4096, "<tr><td>\xF0\x9F\x93\x81 <a href='/?d=%s'>%s</a></td><td class=s></td><td class=s><a class=del href='/del?p=%s' onclick=\"return confirm('Apagar a pasta (precisa estar vazia)?')\">apagar</a></td></tr>", eu2, en, eu2);
			} else {
				char full[600], sz[32];
				struct stat st;
				snprintf(full, sizeof(full), "%s%s", path, e->d_name);
				long s = stat(full, &st) ? 0 : st.st_size;
				if (s < 1024) snprintf(sz, sizeof(sz), "%ld B", s);
				else if (s < 1048576) snprintf(sz, sizeof(sz), "%ld KB", s / 1024);
				else snprintf(sz, sizeof(sz), "%ld.%ld MB", s / 1048576, (s % 1048576) * 10 / 1048576);
				snprintf(b, 4096, "<tr><td>\xF0\x9F\x93\x84 <a href='/f?p=%s'>%s</a></td><td class=s>%s</td><td class=s><a class=del href='/del?p=%s' onclick=\"return confirm('Apagar %s?')\">apagar</a></td></tr>", eu2, en, sz, eu2, en);
			}
			if (sendStr(c, b) < 0) {
				pass = 2;
				break;
			}
			count++;
		}
	}
	closedir(d);
	if (!count) sendStr(c, "<tr><td>(pasta vazia)</td></tr>");
	sendStr(c, "</table></div><p style='color:#888;font-size:13px;text-align:center'>DSi Dash v" APP_VERSION " \xE2\x80\x94 mantenha o DSi com o app Transferir aberto</p></main></body></html>");
	free(b);
}

static void download(Conn* c, const char* rel) {
	char path[400];
	if (!safePath(rel, path, sizeof(path))) {
		simple(c, 400, "caminho invalido");
		return;
	}
	FILE* f = fopen(path, "rb");
	if (!f) {
		simple(c, 404, "arquivo nao encontrado");
		return;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	const char* name = strrchr(path, '/');
	name = name ? name + 1 : path;
	char h[512];
	snprintf(h, sizeof(h), "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %ld\r\nContent-Disposition: attachment; filename=\"%s\"\r\nConnection: close\r\n\r\n", sz, name);
	sendStr(c, h);
	snprintf(s_st.file, sizeof(s_st.file), "%s", name);
	s_st.total = sz;
	s_st.done = 0;
	s_st.active = 2;
	logEvent("\xE2\x86\x91 Enviando %s", name);
	char* buf = (char*)malloc(8192);
	if (buf) {
		size_t n;
		while (s_run && (n = fread(buf, 1, 8192, f)) > 0) {
			if (sendAll(c, buf, n) < 0) break;
			s_st.done += n;
		}
		free(buf);
	}
	fclose(f);
	s_st.active = 0;
	s_st.downloads++;
}

// upload multipart/form-data, gravando direto no cartao
static void upload(Conn* c, const char* rel, const char* ctype, long clen, char* pre, int preLen) {
	char dir[400];
	if (!safePath(rel, dir, sizeof(dir))) {
		simple(c, 400, "caminho invalido");
		return;
	}
	const char* bp = strstr(ctype, "boundary=");
	if (!bp) {
		simple(c, 400, "sem boundary");
		return;
	}
	bp += 9;
	int blen;
	if (*bp == '"') {
		bp++;
		blen = strcspn(bp, "\"");
	} else {
		blen = strcspn(bp, "; \r\n");
	}
	char delim[100];
	int dl = snprintf(delim, sizeof(delim), "\r\n--%.*s", MIN(blen, 90), bp);
	const int CAP = 32 * 1024;
	char* buf = (char*)malloc(CAP);
	if (!buf) {
		simple(c, 500, "sem memoria");
		return;
	}
	// o corpo comeca com "--boundary": prefixa CRLF para tratar tudo como delimitador
	buf[0] = '\r';
	buf[1] = '\n';
	int have = 2;
	int take = MIN(preLen, CAP - have);
	memcpy(buf + have, pre, take);
	have += take;
	long left = clen - preLen;
	enum { ST_DELIM, ST_HEAD, ST_DATA, ST_END } st = ST_DELIM;
	FILE* f = NULL;
	char fname[128] = "";
	int saved = 0;
	s_st.total = clen;
	s_st.done = preLen;
	s_st.active = 1;
	bool ok = true;
	while (st != ST_END && ok) {
		// completa o buffer
		if (have < CAP && left > 0) {
			int n = sockReadRaw(c, buf + have, MIN((long)(CAP - have), left));
			if (n <= 0) {
				ok = false;
				break;
			}
			have += n;
			left -= n;
			s_st.done += n;
		}
		if (st == ST_DELIM) {
			if (have < dl + 2) {
				if (left <= 0) break;
				continue;
			}
			if (memcmp(buf, delim, dl)) {
				ok = false;
				break;
			}
			if (buf[dl] == '-' && buf[dl + 1] == '-') {
				st = ST_END;
				break;
			}
			memmove(buf, buf + dl + 2, have - dl - 2);
			have -= dl + 2;
			st = ST_HEAD;
		} else if (st == ST_HEAD) {
			char* e = memmem(buf, have, "\r\n\r\n", 4);
			if (!e) {
				if (have >= CAP || left <= 0) {
					ok = false;
					break;
				}
				continue;
			}
			*e = 0;
			char* fn = strcasestr(buf, "filename=\"");
			fname[0] = 0;
			if (fn) {
				fn += 10;
				char* q = strchr(fn, '"');
				int n = q ? (int)(q - fn) : 0;
				// so o nome (navegadores antigos mandam o caminho completo)
				char* sl = memrchr(fn, '\\', n);
				if (!sl) sl = memrchr(fn, '/', n);
				if (sl) {
					n -= (sl + 1 - fn);
					fn = sl + 1;
				}
				snprintf(fname, sizeof(fname), "%.*s", MIN(n, 127), fn);
				for (char* x = fname; *x; x++)
					if (strchr(":*?\"<>|", *x)) *x = '_';
			}
			int hl = (e - buf) + 4;
			memmove(buf, buf + hl, have - hl);
			have -= hl;
			if (fname[0]) {
				char full[600];
				snprintf(full, sizeof(full), "%s%s%s", dir, (dir[strlen(dir) - 1] == '/') ? "" : "/", fname);
				f = fopen(full, "wb");
				snprintf(s_st.file, sizeof(s_st.file), "%s", fname);
				logEvent("\xE2\x86\x93 Recebendo %s", fname);
			}
			st = ST_DATA;
		} else if (st == ST_DATA) {
			char* e = memmem(buf, have, delim, dl);
			if (e) {
				int n = e - buf;
				if (f) {
					fwrite(buf, 1, n, f);
					fclose(f);
					f = NULL;
					saved++;
					logEvent("\xE2\x9C\x93 Salvo %s", fname);
				}
				memmove(buf, e, have - n);
				have -= n;
				st = ST_DELIM;
			} else {
				int keep = dl + 2;
				if (have > keep) {
					int n = have - keep;
					if (f && fwrite(buf, 1, n, f) != (size_t)n) {
						ok = false;
						break;
					}
					memmove(buf, buf + n, keep);
					have = keep;
				}
				if (left <= 0) {
					ok = false;
					break;
				}
			}
		}
	}
	if (f) fclose(f);
	free(buf);
	s_st.active = 0;
	s_st.uploads += saved;
	char msg[160];
	if (ok && saved) snprintf(msg, sizeof(msg), "%d arquivo(s) enviado(s) com sucesso", saved);
	else snprintf(msg, sizeof(msg), "Falha no envio (conex\xC3\xA3o interrompida ou cart\xC3\xA3o cheio?)");
	if (!ok) logEvent("%s", "\xE2\x9C\x97 Envio interrompido");
	redirect(c, rel, msg);
}

// ---------------------------------------------------------------------------
static void handle(int sock) {
	Conn c = {0};
	c.sock = sock;
	c.timeoutMs = 20000;
	int one = 1;
	ioctl(sock, FIONBIO, &one);
	char* req = (char*)malloc(8192 + 1);
	if (!req) return;
	int have = 0;
	char* end = NULL;
	while (have < 8192) {
		int n = sockReadRaw(&c, req + have, 8192 - have);
		if (n <= 0) break;
		have += n;
		req[have] = 0;
		if ((end = strstr(req, "\r\n\r\n"))) break;
	}
	if (!end) {
		free(req);
		return;
	}
	*end = 0;
	char* body = end + 4;
	int bodyHave = have - (body - req);
	char method[8] = "", target[1024] = "";
	sscanf(req, "%7s %1023s", method, target);
	long clen = 0;
	char ctype[200] = "";
	for (char* l = strstr(req, "\r\n"); l; l = strstr(l + 2, "\r\n")) {
		if (!strncasecmp(l + 2, "Content-Length:", 15)) clen = atol(l + 17);
		else if (!strncasecmp(l + 2, "Content-Type:", 13)) snprintf(ctype, sizeof(ctype), "%.*s", (int)strcspn(l + 15, "\r\n"), l + 15);
	}
	char* q = strchr(target, '?');
	if (q) *q++ = 0;
	char d[400], m[200], p[400];
	queryParam(q, "d", d, sizeof(d));
	s_st.requests++;
	if (!strcmp(method, "GET") && !strcmp(target, "/")) {
		queryParam(q, "m", m, sizeof(m));
		listing(&c, d, m);
	} else if (!strcmp(method, "GET") && !strcmp(target, "/f")) {
		queryParam(q, "p", p, sizeof(p));
		download(&c, p);
	} else if (!strcmp(method, "GET") && !strcmp(target, "/del")) {
		queryParam(q, "p", p, sizeof(p));
		char full[500];
		bool ok = safePath(p, full, sizeof(full)) && (remove(full) == 0 || rmdir(full) == 0);
		char* sl = strrchr(p, '/');
		if (sl) *sl = 0;
		else p[0] = 0;
		if (ok) logEvent("Apagado: %s", strrchr(full, '/') ? strrchr(full, '/') + 1 : full);
		redirect(&c, p, ok ? "Apagado" : "N\xC3\xA3o foi poss\xC3\xADvel apagar");
	} else if (!strcmp(method, "GET") && !strcmp(target, "/mk")) {
		char n[128], full[600];
		queryParam(q, "n", n, sizeof(n));
		bool ok = n[0] && !strchr(n, '/') && safePath(d, full, sizeof(full));
		if (ok) {
			int l = strlen(full);
			snprintf(full + l, sizeof(full) - l, "%s%s", (l && full[l - 1] == '/') ? "" : "/", n);
			ok = mkdir(full, 0777) == 0;
		}
		redirect(&c, d, ok ? "Pasta criada" : "N\xC3\xA3o foi poss\xC3\xADvel criar a pasta");
	} else if (!strcmp(method, "POST") && !strcmp(target, "/up")) {
		upload(&c, d, ctype, clen, body, bodyHave);
	} else {
		simple(&c, 404, "nao encontrado");
	}
	free(req);
}

static int srvMain(void* arg) {
	s_alive = true;
	int ls = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	s_port = 80;
	sa.sin_port = htons(s_port);
	if (ls < 0 || bind(ls, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
		s_port = 8080;
		sa.sin_port = htons(s_port);
		if (ls < 0 || bind(ls, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
			if (ls >= 0) closesocket(ls);
			s_port = 0;
			s_alive = false;
			return 0;
		}
	}
	listen(ls, 3);
	int one = 1;
	ioctl(ls, FIONBIO, &one);
	logEvent("%s", "Servidor pronto");
	while (s_run) {
		struct sockaddr_in ca;
		socklen_t cl = sizeof(ca);
		int cs = accept(ls, (struct sockaddr*)&ca, &cl);
		if (cs < 0) {
			threadSleep(15000);
			continue;
		}
		handle(cs);
		shutdown(cs, SHUT_RDWR);
		closesocket(cs);
	}
	closesocket(ls);
	s_alive = false;
	return 0;
}

bool srvStart(void) {
	if (s_alive) return true;
	memset(&s_st, 0, sizeof(s_st));
	s_run = true;
	threadPrepare(&s_thr, srvMain, NULL, &s_stack[sizeof(s_stack)], MAIN_THREAD_PRIO + 1);
	static void* tls;
	size_t tsz = threadGetLocalStorageSize();
	if (tsz && !tls) tls = memalign(8, tsz);
	if (tls) threadAttachLocalStorage(&s_thr, tls);
	threadStart(&s_thr);
	return true;
}

void srvStop(void) {
	s_run = false;
	for (int i = 0; i < 180 && s_alive; i++) threadWaitForVBlank();
}

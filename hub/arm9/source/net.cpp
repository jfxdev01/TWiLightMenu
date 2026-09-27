#ifdef __NDS__

#include "net.h"

#include <curl/curl.h>
#include <dswifi9.h>
#include <nds.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "cacert_bin.h"
#include "config.h"
#include "i18n.h"
#include "platform.h"
#include "ui.h"

namespace net {

static bool inited = false;
static bool inDsiMode = false;
static std::string initErr;
static State curState = STATE_OFF;
static std::string curSsid;
static uint32_t scanStartFrame = 0;
static bool autoConnecting = false;
static bool triedWfc = false;
static bool curlReady = false;

bool init() {
	if (inited)
		return true;
	u32 flags = INIT_ONLY;
	bool dsi = platform::isDSi() && config::dsiWifi();
	flags |= dsi ? WIFI_ATTEMPT_DSI_MODE : WIFI_DS_MODE_ONLY;
	if (!Wifi_InitDefault(flags)) {
		initErr = TR("The WiFi hardware couldn't be started.", "Não foi possível iniciar o hardware Wi-Fi.");
		return false;
	}
	inited = true;
	inDsiMode = dsi;
	Wifi_EnableWifi();
	return true;
}

bool initialized() { return inited; }
bool dsiMode() { return inDsiMode; }
std::string initError() { return initErr; }

State state() { return curState; }

static int barsFromRssi(int rssi) {
	if (inDsiMode) {
		// dBm
		if (rssi >= -60)
			return 3;
		if (rssi >= -70)
			return 2;
		if (rssi >= -80)
			return 1;
		return 0;
	}
	if (rssi >= 40)
		return 3;
	if (rssi >= 25)
		return 2;
	if (rssi >= 10)
		return 1;
	return 0;
}

int signalBars() {
	if (!inited || curState != STATE_CONNECTED)
		return -1;
	int rssi = Wifi_GetData(WIFIGETDATA_RSSI, 0, NULL);
	if (inDsiMode && rssi > 0)
		rssi = -rssi;
	return barsFromRssi(rssi);
}

std::string ssid() { return curSsid; }

std::string ipAddress() {
	if (!inited || curState != STATE_CONNECTED)
		return "";
	struct in_addr ip = {Wifi_GetIP()};
	return inet_ntoa(ip);
}

std::string gateway() {
	struct in_addr gw, mask, d1, d2;
	Wifi_GetIPInfo(&gw, &mask, &d1, &d2);
	return inet_ntoa(gw);
}

std::string dns() {
	struct in_addr gw, mask, d1, d2;
	Wifi_GetIPInfo(&gw, &mask, &d1, &d2);
	return inet_ntoa(d1);
}

std::string macAddress() {
	if (!inited)
		return "";
	u8 mac[6];
	Wifi_GetData(WIFIGETDATA_MACADDRESS, 6, mac);
	char buf[24];
	snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buf;
}

// Saved networks ------------------------------------------------------------

void savePassword(const std::string &s, const std::string &password) {
	config::ini().set("WIFI_NETWORKS", s, password);
	config::save();
}

void forget(const std::string &s) {
	config::ini().remove("WIFI_NETWORKS", s);
	config::save();
}

std::vector<std::string> savedNetworks() {
	std::vector<std::string> out;
	for (auto &kv : config::ini().entries("WIFI_NETWORKS"))
		out.push_back(kv.first);
	return out;
}

static bool savedPassword(const std::string &s, std::string &pw) {
	for (auto &kv : config::ini().entries("WIFI_NETWORKS")) {
		if (kv.first == s) {
			pw = kv.second;
			return true;
		}
	}
	return false;
}

// Scanning ------------------------------------------------------------------

void startScan() {
	if (!init())
		return;
	Wifi_ScanModeFilter(WSCAN_LIST_AP_ALL);
	Wifi_ScanMode();
	scanStartFrame = platform::frame();
	if (curState == STATE_CONNECTED || curState == STATE_CONNECTING)
		curState = STATE_OFF;
}

std::vector<AccessPoint> scanResults() {
	std::vector<AccessPoint> out;
	if (!inited)
		return out;
	int n = Wifi_GetNumAP();
	for (int i = 0; i < n; i++) {
		Wifi_AccessPoint ap;
		if (Wifi_GetAPData(i, &ap) != WIFI_RETURN_OK)
			continue;
		if (ap.ssid_len == 0)
			continue; // hidden network
		AccessPoint a;
		a.ssid = std::string(ap.ssid, ap.ssid_len);
		a.rssi = ap.rssi;
		a.bars = barsFromRssi(ap.rssi);
		a.secured = ap.security_type != AP_SECURITY_OPEN;
		a.supported = (ap.flags & WFLAG_APDATA_COMPATIBLE) != 0;
		std::string pw;
		a.saved = savedPassword(a.ssid, pw) || (ap.flags & WFLAG_APDATA_CONFIG_IN_WFC);
		a.security = Wifi_ApSecurityTypeString(ap.security_type);
		a.index = i;
		// Keep the strongest entry for each SSID
		bool dup = false;
		for (AccessPoint &o : out) {
			if (o.ssid == a.ssid) {
				dup = true;
				if (a.rssi > o.rssi)
					o = a;
				break;
			}
		}
		if (!dup)
			out.push_back(a);
	}
	return out;
}

// Connecting ----------------------------------------------------------------

static void connectToIndex(int index, const std::string &password, bool useWfc) {
	Wifi_AccessPoint ap;
	if (Wifi_GetAPData(index, &ap) != WIFI_RETURN_OK) {
		curState = STATE_FAILED;
		return;
	}
	curSsid = std::string(ap.ssid, ap.ssid_len);
	Wifi_SetIP(0, 0, 0, 0, 0);
	if (useWfc)
		Wifi_ConnectWfcAP(&ap);
	else if (ap.security_type == AP_SECURITY_OPEN)
		Wifi_ConnectSecureAP(&ap, NULL, 0);
	else
		Wifi_ConnectSecureAP(&ap, password.c_str(), password.size());
	curState = STATE_CONNECTING;
}

void connectTo(const AccessPoint &a, const std::string &password) {
	if (!init())
		return;
	autoConnecting = false;
	std::string pw = password;
	bool wfc = false;
	if (pw.empty() && a.secured && !savedPassword(a.ssid, pw))
		wfc = true; // only in the console's connection settings
	connectToIndex(a.index, pw, wfc);
}

void connectAuto() {
	if (!init()) {
		curState = STATE_FAILED;
		return;
	}
	if (curState == STATE_CONNECTED)
		return;
	triedWfc = false;
	autoConnecting = true;
	if (savedNetworks().empty()) {
		// Nothing saved in the Hub: use the console's connection settings
		triedWfc = true;
		curSsid.clear();
		Wifi_AutoConnect();
		curState = STATE_CONNECTING;
		return;
	}
	startScan();
	curState = STATE_SCANNING;
}

void disconnect() {
	if (inited)
		Wifi_DisconnectAP();
	autoConnecting = false;
	curState = STATE_OFF;
}

void update() {
	if (!inited)
		return;
	switch (curState) {
		case STATE_SCANNING: {
			// Give the scan ~3 seconds to find saved networks
			if (platform::frame() - scanStartFrame < 180)
				break;
			std::vector<AccessPoint> aps = scanResults();
			const AccessPoint *best = nullptr;
			std::string pw;
			for (const AccessPoint &a : aps) {
				std::string p;
				if (a.supported && savedPassword(a.ssid, p) && (!best || a.rssi > best->rssi)) {
					best = &a;
					pw = p;
				}
			}
			if (best) {
				connectToIndex(best->index, pw, false);
			} else {
				triedWfc = true;
				curSsid.clear();
				Wifi_AutoConnect();
				curState = STATE_CONNECTING;
			}
			break;
		}
		case STATE_CONNECTING: {
			int s = Wifi_AssocStatus();
			if (s == ASSOCSTATUS_ASSOCIATED) {
				curState = STATE_CONNECTED;
				autoConnecting = false;
				if (curSsid.empty()) {
					// Connected through the console settings: find which AP
					Wifi_AccessPoint ap;
					for (int i = 0; i < Wifi_GetNumAP(); i++) {
						if (Wifi_GetAPData(i, &ap) == WIFI_RETURN_OK && (ap.flags & WFLAG_APDATA_ACTIVE)) {
							curSsid = std::string(ap.ssid, ap.ssid_len);
							break;
						}
					}
				}
			} else if (s == ASSOCSTATUS_CANNOTCONNECT) {
				if (autoConnecting && !triedWfc) {
					triedWfc = true;
					curSsid.clear();
					Wifi_AutoConnect();
				} else {
					curState = STATE_FAILED;
					autoConnecting = false;
				}
			}
			break;
		}
		default:
			break;
	}
}

std::string stateText() {
	switch (curState) {
		case STATE_OFF:
			return TR("Not connected", "Desconectado");
		case STATE_SCANNING:
			return TR("Looking for networks…", "Procurando redes…");
		case STATE_CONNECTING: {
			int s = Wifi_AssocStatus();
			if (s == ASSOCSTATUS_ACQUIRINGDHCP)
				return TR("Getting an IP address…", "Obtendo endereço IP…");
			if (s == ASSOCSTATUS_AUTHENTICATING || s == ASSOCSTATUS_ASSOCIATING)
				return TR("Authenticating…", "Autenticando…");
			return TR("Connecting…", "Conectando…");
		}
		case STATE_CONNECTED:
			return std::string(TR("Connected to ", "Conectado a ")) + curSsid;
		case STATE_FAILED:
			return TR("Couldn't connect", "Não foi possível conectar");
	}
	return "";
}

bool ensureConnected() {
	if (curState == STATE_CONNECTED)
		return true;
	if (!init()) {
		ui::message(TR("Wi-Fi", "Wi-Fi"), initErr);
		return false;
	}
	connectAuto();
	while (true) {
		platform::scanInput();
		update();
		if (curState == STATE_CONNECTED) {
			ui::toast(stateText());
			return true;
		}
		if (curState == STATE_FAILED) {
			ui::message(TR("Couldn't connect", "Sem conexão"),
			            TR("No known network was found. Open the Wi-Fi app in the Hub to choose a network and type its password.",
			               "Nenhuma rede conhecida foi encontrada. Abra o app Wi-Fi do Hub para escolher uma rede e digitar a senha."));
			return false;
		}
		ui::busy(TR("Connecting to the internet", "Conectando à internet"), stateText(), -1, true);
		if ((platform::keysDown() | ui::footerTapped()) & KEY_B) {
			disconnect();
			return false;
		}
	}
}

// HTTP ----------------------------------------------------------------------

struct Transfer {
	const Request *req;
	Response *res;
	FILE *file;
	size_t written;
	bool aborted;
};

static size_t writeCb(char *data, size_t size, size_t nmemb, void *user) {
	Transfer *t = (Transfer *)user;
	size_t n = size * nmemb;
	if (t->written + n > t->req->maxBytes) {
		t->res->truncated = true;
		size_t room = t->req->maxBytes - t->written;
		if (t->file)
			fwrite(data, 1, room, t->file);
		else
			t->res->body.append(data, room);
		t->written += room;
		return 0; // stop
	}
	if (t->file) {
		if (fwrite(data, 1, n, t->file) != n)
			return 0;
	} else {
		t->res->body.append(data, n);
	}
	t->written += n;
	return n;
}

static int progressCb(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
	Transfer *t = (Transfer *)user;
	if (t->req->progress && !t->req->progress(t->req->user, (size_t)dlnow, (size_t)dltotal)) {
		t->aborted = true;
		return 1;
	}
	return 0;
}

static std::string userCaFile() {
	return config::hubDir() + "/cacert.pem";
}

bool fetch(const Request &req, Response &res) {
	res = Response();
	if (!curlReady) {
		curl_global_init(CURL_GLOBAL_ALL);
		curlReady = true;
	}
	CURL *curl = curl_easy_init();
	if (!curl) {
		res.error = "curl_easy_init";
		return false;
	}
	Transfer t{&req, &res, nullptr, 0, false};
	if (!req.outputFile.empty()) {
		t.file = fopen(req.outputFile.c_str(), "wb");
		if (!t.file) {
			res.error = TR("Couldn't create the file", "Não foi possível criar o arquivo");
			curl_easy_cleanup(curl);
			return false;
		}
	}
	char errbuf[CURL_ERROR_SIZE] = {0};
	curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Nintendo DSi; TWiLight Hub) Mobile");
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 32L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCb);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &t);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	if (!req.postData.empty())
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.postData.c_str());

	if (req.verifyTls) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		if (access(userCaFile().c_str(), F_OK) == 0) {
			curl_easy_setopt(curl, CURLOPT_CAINFO, userCaFile().c_str());
		} else {
			struct curl_blob blob;
			blob.data = (void *)cacert_bin;
			blob.len = cacert_bin_size;
			blob.flags = CURL_BLOB_NOCOPY;
			curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);
		}
	} else {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	CURLcode rc = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
	char *ct = nullptr;
	if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct) == CURLE_OK && ct)
		res.contentType = ct;
	char *eff = nullptr;
	if (curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff) == CURLE_OK && eff)
		res.url = eff;
	curl_easy_cleanup(curl);
	if (t.file)
		fclose(t.file);

	bool https = res.url.compare(0, 8, "https://") == 0 || req.url.compare(0, 8, "https://") == 0;
	res.verified = https && req.verifyTls;

	if (rc == CURLE_WRITE_ERROR && res.truncated)
		return true; // partial content is fine (maxBytes)
	if (rc != CURLE_OK) {
		if (t.aborted)
			res.error = TR("Cancelled", "Cancelado");
		else
			res.error = errbuf[0] ? errbuf : curl_easy_strerror(rc);
		res.certError = (rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CACERT_BADFILE);
		if (t.file)
			remove(req.outputFile.c_str());
		return false;
	}
	return true;
}

struct UiProgress {
	std::string title;
	std::string host;
	ProgressFn inner;
	void *innerUser;
	int frameSkip;
};

static bool uiProgress(void *user, size_t done, size_t total) {
	UiProgress *p = (UiProgress *)user;
	platform::scanInput();
	if ((platform::keysDown() | ui::footerTapped()) & KEY_B)
		return false;
	if (p->inner && !p->inner(p->innerUser, done, total))
		return false;
	// Redrawing is slow, don't do it on every callback
	if (++p->frameSkip % 4)
		return true;
	char info[64];
	if (total > 0)
		snprintf(info, sizeof(info), "%u / %u KB", (unsigned)(done / 1024), (unsigned)(total / 1024));
	else
		snprintf(info, sizeof(info), "%u KB", (unsigned)(done / 1024));
	ui::busy(p->title, p->host + "\n" + info, total > 0 ? (int)(done * 100 / total) : -1, true);
	return true;
}

bool fetchWithUi(const std::string &title, Request req, Response &res) {
	UiProgress p{title, "", req.progress, req.user, 0};
	std::string u = req.url;
	size_t s = u.find("://");
	p.host = s == std::string::npos ? u : u.substr(s + 3, u.find('/', s + 3) - (s + 3));
	ui::busy(title, p.host, -1, true);
	req.progress = uiProgress;
	req.user = &p;
	return fetch(req, res);
}

std::string urlEncode(const std::string &s) {
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
			out += (char)c;
		} else if (c == ' ') {
			out += '+';
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

// NTP -----------------------------------------------------------------------

bool ntpTime(const std::string &server, time_t &utc, std::string &error) {
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	struct addrinfo *result = nullptr;
	if (getaddrinfo(server.c_str(), "123", &hints, &result) != 0 || !result) {
		error = TR("Couldn't find the time server", "Servidor de hora não encontrado");
		return false;
	}
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		freeaddrinfo(result);
		error = "socket()";
		return false;
	}
	uint8_t packet[48];
	memset(packet, 0, sizeof(packet));
	packet[0] = 0x1B; // LI = 0, VN = 3, Mode = 3 (client)
	bool ok = false;
	for (int attempt = 0; attempt < 3 && !ok; attempt++) {
		sendto(sock, packet, sizeof(packet), 0, result->ai_addr, result->ai_addrlen);
		// Wait up to 2 seconds for the answer, letting the WiFi thread run
		for (int f = 0; f < 120; f++) {
			uint8_t reply[48];
			int n = recvfrom(sock, reply, sizeof(reply), MSG_DONTWAIT, nullptr, nullptr);
			if (n >= 48) {
				uint32_t secs = ((uint32_t)reply[40] << 24) | ((uint32_t)reply[41] << 16) | ((uint32_t)reply[42] << 8) | reply[43];
				if (secs != 0) {
					// NTP epoch is 1900, Unix epoch is 1970
					utc = (time_t)(secs - 2208988800UL);
					ok = true;
				}
				break;
			}
			platform::waitVBlank();
		}
	}
	closesocket(sock);
	freeaddrinfo(result);
	if (!ok)
		error = TR("The time server didn't answer", "O servidor de hora não respondeu");
	return ok;
}

} // namespace net

#endif // __NDS__

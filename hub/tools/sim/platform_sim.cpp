// PC implementation of platform.h and stubs of net.h, used to render the UI
// of TWiLight Hub into PNG files. Driven by a script in $HUB_SIM_SCRIPT:
//   wait N        run N frames
//   key NAME      press a button for one frame (A B X Y L R UP DOWN LEFT RIGHT START SELECT)
//   touch X Y     tap the bottom screen
//   shot NAME     save out/NAME.png (both screens, 2x)
//   quit          exit
// Pages for the browser are read from fixtures/<host>.html.

#include "../../arm9/source/platform.h"
#include "../../arm9/source/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <deque>
#include <sstream>
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../arm9/source/stb_image_write.h"

namespace platform {

static uint16_t topBuf[256 * 192], botBuf[256 * 192];
static Surface topS, botS;
static uint32_t frameNo = 0;
static uint32_t kDown, kHeld;
static std::deque<uint32_t> keyQueue;
static int touchPhase = 0, tx, ty, pendingTx, pendingTy;
static bool touchNow, touchBefore;
static std::vector<std::string> script;
static size_t scriptPos = 0;
static int waitFrames = 0;
static std::string rootPath = "sim:", twlPath = "sim:/_nds/TWiLightMenu";

static void saveShot(const std::string &name) {
	mkdir("out", 0777);
	const int W = 512, H = 384 * 2 + 16;
	std::vector<uint8_t> rgb(W * H * 3, 0x20);
	auto put = [&](uint16_t *buf, int oy) {
		for (int y = 0; y < 192; y++)
			for (int x = 0; x < 256; x++) {
				uint16_t c = buf[y * 256 + x];
				uint8_t r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
				for (int dy = 0; dy < 2; dy++)
					for (int dx = 0; dx < 2; dx++) {
						uint8_t *p = &rgb[((oy + y * 2 + dy) * W + x * 2 + dx) * 3];
						p[0] = r, p[1] = g, p[2] = b;
					}
			}
	};
	put(topBuf, 0);
	put(botBuf, 384 + 16);
	stbi_write_png(("out/" + name + ".png").c_str(), W, H, 3, rgb.data(), W * 3);
	printf("saved out/%s.png\n", name.c_str());
}

static uint32_t keyByName(const std::string &n) {
	static const struct { const char *n; uint32_t k; } keys[] = {
		{"A", KEY_A}, {"B", KEY_B}, {"X", KEY_X}, {"Y", KEY_Y}, {"L", KEY_L}, {"R", KEY_R},
		{"UP", KEY_UP}, {"DOWN", KEY_DOWN}, {"LEFT", KEY_LEFT}, {"RIGHT", KEY_RIGHT},
		{"START", KEY_START}, {"SELECT", KEY_SELECT},
	};
	for (auto &k : keys)
		if (n == k.n)
			return k.k;
	return 0;
}

static void runScript() {
	if (waitFrames > 0) {
		waitFrames--;
		return;
	}
	while (scriptPos < script.size()) {
		std::istringstream is(script[scriptPos++]);
		std::string cmd;
		is >> cmd;
		if (cmd == "wait") {
			is >> waitFrames;
			return;
		} else if (cmd == "key") {
			std::string k;
			is >> k;
			keyQueue.push_back(keyByName(k));
			return;
		} else if (cmd == "touch") {
			is >> pendingTx >> pendingTy;
			touchPhase = 1;
			return;
		} else if (cmd == "shot") {
			std::string n;
			is >> n;
			saveShot(n);
		} else if (cmd == "quit") {
			exit(0);
		}
	}
	if (scriptPos >= script.size()) {
		fprintf(stderr, "script finished\n");
		exit(0);
	}
}

void init(int, char **) {
	topS.init(topBuf, 256, 192);
	botS.init(botBuf, 256, 192);
	const char *s = getenv("HUB_SIM_SCRIPT");
	std::string all = s ? s : "wait 5; shot home; quit";
	std::stringstream ss(all);
	std::string item;
	while (std::getline(ss, item, ';')) {
		size_t a = item.find_first_not_of(' ');
		if (a != std::string::npos)
			script.push_back(item.substr(a));
	}
	mkdir("sim:", 0777);
	mkdir("sim:/_nds", 0777);
	mkdir("sim:/_nds/TWiLightMenu", 0777);
}

Surface &top() { return topS; }
Surface &bottom() { return botS; }
void present(bool, bool) {
	frameNo++;
	runScript();
}
void waitVBlank() {
	frameNo++;
	runScript();
}
uint32_t frame() { return frameNo; }

void scanInput() {
	kDown = 0;
	if (!keyQueue.empty()) {
		kDown = keyQueue.front();
		keyQueue.pop_front();
	}
	kHeld = kDown;
	touchBefore = touchNow;
	if (touchPhase == 1) {
		touchNow = true;
		tx = pendingTx;
		ty = pendingTy;
		touchPhase = 2;
	} else if (touchPhase == 2) {
		touchNow = false;
		touchPhase = 0;
	}
}
uint32_t keysDown() { return kDown; }
uint32_t keysHeld() { return kHeld; }
uint32_t keysRepeat() { return kDown; }
bool touching() { return touchNow; }
bool touchDown() { return touchNow && !touchBefore; }
bool touchReleased() { return !touchNow && touchBefore; }
int touchX() { return tx; }
int touchY() { return ty; }

bool isDSi() { return true; }
int batteryLevel() { return 87; }
bool charging() { return false; }
std::string nickname() { return "Jéssica"; }
uint16_t favoriteColor() { return 0x839E; }
int firmwareLanguage() { return 1; }
std::string consoleName() { return "Nintendo DSi"; }
bool macAddress(uint8_t *) { return false; }
time_t now() { return 1790000000; }
bool setClock(const struct tm &) { return true; }
const std::string &root() { return rootPath; }
const std::string &twlDir() { return twlPath; }
bool sdFreeSpace(uint64_t &f, uint64_t &t) {
	f = 3ULL << 30;
	t = 8ULL << 30;
	return true;
}
bool chainload(const std::string &, const std::vector<std::string> &) { return false; }
void exitToMenu() { exit(0); }
void setReturnPath(const std::string &) {}
static uint16_t fakeVram[256 * 192];
uint16_t *topVram() { return fakeVram; }
void setTopPassthrough(bool) {}
void setBrightness(int, int) {}
void playSfx(Sfx) {}

} // namespace platform

// ---------------------------------------------------------------------------

namespace net {

static State st = STATE_OFF;
bool init() { return true; }
bool initialized() { return st != STATE_OFF; }
bool dsiMode() { return true; }
std::string initError() { return ""; }
State state() { return st; }
int signalBars() { return st == STATE_CONNECTED ? 3 : -1; }
std::string ssid() { return "Casa 5G"; }
std::string ipAddress() { return "192.168.0.42"; }
std::string gateway() { return "192.168.0.1"; }
std::string dns() { return "192.168.0.1"; }
std::string macAddress() { return "00:24:1E:12:34:56"; }
void connectAuto() { st = STATE_CONNECTED; }
void connectTo(const AccessPoint &, const std::string &) { st = STATE_CONNECTED; }
void disconnect() { st = STATE_OFF; }
void update() {}
std::string stateText() { return st == STATE_CONNECTED ? "Conectado a Casa 5G" : "Desconectado"; }
void startScan() {}
std::vector<AccessPoint> scanResults() {
	return {
		{"Casa 5G", -48, 3, true, true, true, "WPA2", 0},
		{"Vizinho_2.4", -71, 1, true, true, false, "WPA2", 1},
		{"Cafe Aberto", -63, 2, false, true, false, "Open", 2},
		{"NET_VIRTUA_99", -82, 0, true, false, false, "WPA", 3},
	};
}
void savePassword(const std::string &, const std::string &) {}
void forget(const std::string &) {}
std::vector<std::string> savedNetworks() { return {"Casa 5G"}; }
bool ensureConnected() {
	st = STATE_CONNECTED;
	return true;
}

bool fetch(const Request &req, Response &res) {
	res = Response();
	std::string u = req.url;
	size_t s = u.find("://");
	std::string host = s == std::string::npos ? u : u.substr(s + 3);
	host = host.substr(0, host.find_first_of("/?"));
	std::string path = "fixtures/" + host + ".html";
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) {
		res.error = "no fixture " + path;
		return false;
	}
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		res.body.append(buf, n);
	fclose(f);
	res.status = 200;
	res.url = req.url;
	res.contentType = "text/html; charset=utf-8";
	res.verified = u.compare(0, 8, "https://") == 0;
	if (!req.outputFile.empty()) {
		FILE *o = fopen(req.outputFile.c_str(), "wb");
		if (o) {
			fwrite(res.body.data(), 1, res.body.size(), o);
			fclose(o);
		}
	}
	return true;
}

bool fetchWithUi(const std::string &, Request req, Response &res) { return fetch(req, res); }

std::string urlEncode(const std::string &s) {
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			out += (char)c;
		else if (c == ' ')
			out += '+';
		else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

bool ntpTime(const std::string &, time_t &utc, std::string &) {
	utc = 1790000000;
	return true;
}

} // namespace net

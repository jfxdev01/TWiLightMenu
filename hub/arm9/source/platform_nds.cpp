// Console implementation of platform.h (BlocksDS)
#ifdef __NDS__

#include "platform.h"

#include <fat.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "config.h"
#include "ipc_commands.h"

namespace platform {

static uint16_t topBuffer[256 * 192] __attribute__((aligned(32)));
static uint16_t bottomBuffer[256 * 192] __attribute__((aligned(32)));
static Surface topSurface, bottomSurface;
static uint16_t *topVramPtr, *bottomVramPtr;
static bool topPassthrough = false;
static uint32_t frameCounter = 0;

static uint32_t kDown, kHeld, kRepeat;
static bool touchNow, touchBefore;
static int tX, tY;

static std::string rootPath = "sd:";
static std::string twlPath = "sd:/_nds/TWiLightMenu";
static std::string returnPath;

// ---------------------------------------------------------------------------
// Sound effects (generated at startup, 8-bit PCM)
// ---------------------------------------------------------------------------

static const int kSampleRate = 16384;

struct SfxData {
	int8_t *data = nullptr;
	uint32_t size = 0;
};
static SfxData sfx[6];

static void makeTone(SfxData &out, float f0, float f1, float seconds, float volume, bool noise) {
	uint32_t n = (uint32_t)(kSampleRate * seconds);
	n = (n + 3) & ~3;
	out.data = (int8_t *)aligned_alloc(4, n);
	out.size = n;
	float phase = 0;
	uint32_t rnd = 0x1234567;
	for (uint32_t i = 0; i < n; i++) {
		float t = (float)i / n;
		float f = f0 + (f1 - f0) * t;
		phase += f / kSampleRate;
		float env = (1.0f - t) * (1.0f - t);
		if (i < 32)
			env *= i / 32.0f;
		float v;
		if (noise) {
			rnd = rnd * 1103515245 + 12345;
			v = ((int)((rnd >> 16) & 0xFF) - 128) / 128.0f;
		} else {
			v = sinf(phase * 6.2831853f);
		}
		out.data[i] = (int8_t)(v * env * volume * 127.0f);
	}
	DC_FlushRange(out.data, n);
}

static void initSfx() {
	soundEnable();
	makeTone(sfx[SFX_MOVE], 1500, 1500, 0.025f, 0.35f, false);
	makeTone(sfx[SFX_SELECT], 880, 1480, 0.07f, 0.45f, false);
	makeTone(sfx[SFX_BACK], 740, 440, 0.07f, 0.45f, false);
	makeTone(sfx[SFX_ERROR], 220, 180, 0.14f, 0.5f, false);
	makeTone(sfx[SFX_SHUTTER], 0, 0, 0.09f, 0.6f, true);
	makeTone(sfx[SFX_TOGGLE], 1100, 1300, 0.04f, 0.4f, false);
}

void playSfx(Sfx s) {
	if (!config::soundEffects())
		return;
	SfxData &d = sfx[s];
	if (d.data)
		soundPlaySample(d.data, SoundFormat_8Bit, d.size, kSampleRate, 100, 64, false, 0);
}

// ---------------------------------------------------------------------------

void init(int argc, char **argv) {
	videoSetMode(MODE_5_2D);
	videoSetModeSub(MODE_5_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);
	int bgTop = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	int bgBottom = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	topVramPtr = bgGetGfxPtr(bgTop);
	bottomVramPtr = bgGetGfxPtr(bgBottom);
	lcdMainOnTop();

	topSurface.init(topBuffer, 256, 192);
	bottomSurface.init(bottomBuffer, 256, 192);
	gfx::clear(topSurface, 0x8000);
	gfx::clear(bottomSurface, 0x8000);

	if (!fatInitDefault())
		rootPath = "fat:";

	// Work out where TWiLight Menu++ is installed from the path we were
	// launched with ("sd:/_nds/TWiLightMenu/hub.srldr" or "fat:/...")
	if (argc > 0 && argv && argv[0]) {
		if (strncmp(argv[0], "fat:", 4) == 0)
			rootPath = "fat:";
		else if (strncmp(argv[0], "sd:", 3) == 0)
			rootPath = "sd:";
	}
	if (rootPath == "sd:" && access("sd:/", F_OK) != 0 && access("fat:/", F_OK) == 0)
		rootPath = "fat:";
	twlPath = rootPath + "/_nds/TWiLightMenu";

	// Optional argv[1]: menu to return to
	if (argc > 1 && argv[1] && argv[1][0])
		returnPath = argv[1];

	keysSetRepeat(20, 5);
	initSfx();
}

Surface &top() { return topSurface; }
Surface &bottom() { return bottomSurface; }
uint16_t *topVram() { return topVramPtr; }
void setTopPassthrough(bool enabled) { topPassthrough = enabled; }

void waitVBlank() {
	cothread_yield_irq(IRQ_VBLANK);
	frameCounter++;
}

uint32_t frame() { return frameCounter; }

void present(bool updateTop, bool updateBottom) {
	waitVBlank();
	if (updateBottom) {
		DC_FlushRange(bottomBuffer, sizeof(bottomBuffer));
		dmaCopyWords(3, bottomBuffer, bottomVramPtr, sizeof(bottomBuffer));
	}
	if (updateTop && !topPassthrough) {
		DC_FlushRange(topBuffer, sizeof(topBuffer));
		dmaCopyWords(3, topBuffer, topVramPtr, sizeof(topBuffer));
	}
}

void setBrightness(int screen, int level) {
	::setBrightness(screen, level);
}

void scanInput() {
	scanKeys();
	kDown = ::keysDown();
	kHeld = ::keysHeld();
	kRepeat = ::keysDownRepeat();
	touchBefore = touchNow;
	touchNow = (kHeld & KEY_TOUCH) != 0;
	if (touchNow) {
		touchPosition t;
		touchRead(&t);
		tX = t.px;
		tY = t.py;
	}
	if (kDown & KEY_LID)
		systemSleep();
}

uint32_t keysDown() { return kDown; }
uint32_t keysHeld() { return kHeld; }
uint32_t keysRepeat() { return kRepeat; }
bool touching() { return touchNow; }
bool touchDown() { return touchNow && !touchBefore; }
bool touchReleased() { return !touchNow && touchBefore; }
int touchX() { return tX; }
int touchY() { return tY; }

bool isDSi() { return isDSiMode(); }

int batteryLevel() {
	u32 level = getBatteryLevel();
	if (isDSiMode())
		return (int)((level & 0xF) * 100 / 15);
	// DS: only "ok" or "low"
	return (level & 1) ? 15 : 100;
}

bool charging() {
	return isDSiMode() && (getBatteryLevel() & BIT(7));
}

std::string nickname() {
	std::string out;
	int len = PersonalData->nameLen;
	if (len > 10)
		len = 10;
	for (int i = 0; i < len; i++) {
		uint16_t c = PersonalData->name[i];
		if (c < 0x80) {
			out += (char)c;
		} else if (c < 0x800) {
			out += (char)(0xC0 | (c >> 6));
			out += (char)(0x80 | (c & 0x3F));
		} else {
			out += (char)(0xE0 | (c >> 12));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		}
	}
	return out;
}

uint16_t favoriteColor() {
	static const uint16_t colors[16] = {
		0xCE0C, 0x8137, 0x8C1F, 0xFE3F, 0x825F, 0x839E, 0x83F5, 0x83E0,
		0x9E80, 0xC769, 0xFAE6, 0xF960, 0xC800, 0xE811, 0xF41A, 0xC81F,
	};
	int c = PersonalData->theme;
	if (c < 0 || c > 15)
		c = 0;
	return colors[c];
}

int firmwareLanguage() { return PersonalData->language; }

std::string consoleName() {
	switch (config::consoleModel()) {
		case 1:
			return "Nintendo DSi (Panda)";
		case 2:
			return "Nintendo 3DS";
		case 3:
			return "New Nintendo 3DS";
		default:
			return isDSiMode() ? "Nintendo DSi" : "Nintendo DS";
	}
}

bool macAddress(uint8_t mac[6]) {
	(void)mac;
	return false;
}

time_t now() { return time(NULL); }

bool setClock(const struct tm &t) {
	rtcTimeAndDate rtc;
	rtc.year = (u8)(t.tm_year + 1900 - 2000);
	rtc.month = (u8)(t.tm_mon + 1);
	rtc.day = (u8)t.tm_mday;
	rtc.weekday = (u8)t.tm_wday;
	rtc.hours = (u8)t.tm_hour;
	rtc.minutes = (u8)t.tm_min;
	rtc.seconds = (u8)t.tm_sec;

	fifoSendDatamsg(FIFO_HUB_RTC, sizeof(rtc), (u8 *)&rtc);
	for (int i = 0; i < 120; i++) {
		if (fifoCheckValue32(FIFO_HUB_RTC))
			return fifoGetValue32(FIFO_HUB_RTC) == 1;
		waitVBlank();
	}
	return false;
}

const std::string &root() { return rootPath; }
const std::string &twlDir() { return twlPath; }

bool sdFreeSpace(uint64_t &freeBytes, uint64_t &totalBytes) {
	struct statvfs st;
	if (statvfs((rootPath + "/").c_str(), &st) != 0)
		return false;
	freeBytes = (uint64_t)st.f_bavail * st.f_frsize;
	totalBytes = (uint64_t)st.f_blocks * st.f_frsize;
	return true;
}

// ---------------------------------------------------------------------------
// Returning to TWiLight Menu++
//
// TWiLight Menu++ launches its .srldr files with a "bootstub" (the same used by
// the NDS Homebrew Menu) at 0x02FF4000. It contains a copy of TWiLight's
// loader, which by default boots BOOT.NDS. We point it to the requested file
// (by FAT cluster, which BlocksDS exposes as st_ino) and pass argv the same way
// TWiLight's own nds_loader does, then exit().
// ---------------------------------------------------------------------------

struct TwlBootstub {
	u64 bootsig;
	VoidFn arm9reboot;
	VoidFn arm7reboot;
	u32 bootaddr;
	u32 bootsize;
};

static void *mainRamMirror(u32 addr) {
	// The bootstub area is hidden behind DTCM, use a main RAM mirror
	u32 offset = addr - 0x02000000;
	return (void *)((isDSiMode() ? 0x0C000000 : 0x02000000) + (isDSiMode() ? offset : (offset & 0x3FFFFF) | 0x400000));
}

bool chainload(const std::string &path, const std::vector<std::string> &args) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return false;

	TwlBootstub *bs = (TwlBootstub *)mainRamMirror(0x02FF4000);
	if (bs->bootsig != 0x62757473746F6F62ULL) // 'bootstub'
		return false;
	if (bs->bootaddr < 0x02FF4000 || bs->bootaddr >= 0x03000000)
		return false;

	u32 *loader = (u32 *)mainRamMirror(bs->bootaddr);
	// The loader starts with a branch over its parameters
	if ((loader[0] & 0xFF000000) != 0xEA000000)
		return false;

	loader[1] = (u32)st.st_ino; // storedFileCluster

	u32 argStart = (loader[4] + 3) & ~3;
	char *argData = (char *)loader + argStart;
	u32 argSize = 0;
	for (const std::string &a : args) {
		memcpy(argData + argSize, a.c_str(), a.size() + 1);
		argSize += a.size() + 1;
	}
	loader[4] = argStart;
	loader[5] = argSize;

	DC_FlushAll();
	exit(0);
}

void setReturnPath(const std::string &path) { returnPath = path; }

void exitToMenu() {
	std::string target = returnPath;
	if (target.empty()) {
		switch (config::twlTheme()) {
			case 2:
			case 6:
				target = twlPath + "/r4menu.srldr";
				break;
			case 3:
				target = twlPath + "/akmenu.srldr";
				break;
			default:
				target = twlPath + "/dsimenu.srldr";
				break;
		}
	}
	setBrightness(3, 16);
	chainload(target, {target});
	// Fall back to the normal exit path (reboots into BOOT.NDS)
	exit(0);
}

} // namespace platform

#endif // __NDS__

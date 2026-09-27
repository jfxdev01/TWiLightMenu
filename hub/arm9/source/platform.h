// Hardware abstraction of TWiLight Hub. platform_nds.cpp implements it on the
// console, tools/sim/platform_sim.cpp on a PC (to render screenshots).
#pragma once

#include <stdint.h>
#include <string>
#include <time.h>
#include <vector>

#include "gfx.h"

#ifdef __NDS__
#include <nds.h>
#else
// Same values as libnds
#define KEY_A      (1 << 0)
#define KEY_B      (1 << 1)
#define KEY_SELECT (1 << 2)
#define KEY_START  (1 << 3)
#define KEY_RIGHT  (1 << 4)
#define KEY_LEFT   (1 << 5)
#define KEY_UP     (1 << 6)
#define KEY_DOWN   (1 << 7)
#define KEY_R      (1 << 8)
#define KEY_L      (1 << 9)
#define KEY_X      (1 << 10)
#define KEY_Y      (1 << 11)
#define KEY_TOUCH  (1 << 12)
#define KEY_LID    (1 << 13)
#endif

namespace platform {

void init(int argc, char **argv);

// Back buffers of both screens (256x192)
Surface &top();
Surface &bottom();
// Waits for the next frame and copies the back buffers to VRAM
void present(bool updateTop = true, bool updateBottom = true);
// Waits for vblank while letting other threads (WiFi) run
void waitVBlank();
uint32_t frame();

// Input
void scanInput();
uint32_t keysDown();
uint32_t keysHeld();
uint32_t keysRepeat();
bool touching();
bool touchDown();     // stylus touched this frame
bool touchReleased(); // stylus released this frame
int touchX();
int touchY();

// System information
bool isDSi();
int batteryLevel(); // 0-100
bool charging();
std::string nickname();
uint16_t favoriteColor();
int firmwareLanguage(); // 0=JP 1=EN 2=FR 3=DE 4=IT 5=ES 6=ZH 7=KO
std::string consoleName();
bool macAddress(uint8_t mac[6]);

// Time
time_t now();
bool setClock(const struct tm &t);

// Storage
const std::string &root();   // "sd:" or "fat:"
const std::string &twlDir(); // "<root>/_nds/TWiLightMenu"
bool sdFreeSpace(uint64_t &freeBytes, uint64_t &totalBytes);

// Launch another TWiLight Menu++ .srldr/.nds file. Only returns on failure.
bool chainload(const std::string &path, const std::vector<std::string> &args);
// Returns to TWiLight Menu++ (the menu the Hub was opened from)
[[noreturn]] void exitToMenu();
void setReturnPath(const std::string &path);

// Camera top screen passthrough: while enabled, present() leaves the top
// screen VRAM alone so the camera can write to it directly.
uint16_t *topVram();
void setTopPassthrough(bool enabled);
void setBrightness(int screen, int level); // screen: 1 top, 2 bottom, 3 both; -16..16

// Sound effects
enum Sfx { SFX_MOVE, SFX_SELECT, SFX_BACK, SFX_ERROR, SFX_SHUTTER, SFX_TOGGLE };
void playSfx(Sfx s);

} // namespace platform

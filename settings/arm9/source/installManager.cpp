#include "installManager.h"

#include <nds.h>
#include <maxmod9.h>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "common/flashcard.h"
#include "common/nds_loader_arm9.h"
#include "common/systemdetails.h"
#include "common/twlmenusettings.h"
#include "graphics/fontHandler.h"
#include "language.h"
#include "myDSiMode.h"
#include "settingsgui.h"
#include "soundeffect.h"

extern int currentTheme;
extern int pressed;
extern touchPosition touch;
extern bool hiyaAutobootFound;
extern void opt_hiya_autoboot_toggle(bool prev, bool next);
extern void opt_set_luma_autoboot(void);

namespace {

enum BootMethod {
	BOOT_FLASHCARD, // DS mode or running from a flashcard
	BOOT_3DS,       // 3DS in DSi mode (TWiLight Menu++ CIA)
	BOOT_FULL,      // DSi with full access: Unlaunch (optionally with hiyaCFW)
	BOOT_EXPLOIT,   // DSi through a DSiWare exploit (e.g. Memory Pit)
};

BootMethod bootMethod() {
	if (!isDSiMode() || !sys().isRunFromSD())
		return BOOT_FLASHCARD;
	if (ms().consoleModel >= 2)
		return BOOT_3DS;
	// Exploits run with the ARM7 SCFG registers locked. Unlaunch and hiyaCFW
	// (which needs Unlaunch) give full access to the hardware.
	return sys().arm7SCFGLocked() ? BOOT_EXPLOIT : BOOT_FULL;
}

const char *root() { return sys().isRunFromSD() ? "sd:" : "fat:"; }

std::string settingsIniPath() { return std::string(root()) + "/_nds/TWiLightMenu/settings.ini"; }
std::string settingsBackupPath() { return settingsIniPath() + ".bak"; }

bool exists(const std::string &path) { return access(path.c_str(), F_OK) == 0; }

bool hiyaFound() { return exists("sd:/hiya.dsi") && exists("sd:/hiya"); }

std::string unlaunchInstaller() {
	static const char *kPaths[] = {
		"sd:/UNLAUNCH.DSI",
		"sd:/_nds/TWiLightMenu/unlaunch/UNLAUNCH.DSI",
		"sd:/_nds/unlaunch.dsi",
	};
	for (const char *p : kPaths)
		if (exists(p))
			return p;
	return "";
}

void playSelect() { mmEffectEx(currentTheme == 4 ? &snd().snd_saturn_select : &snd().snd_select); }
void playLaunch() { mmEffectEx(currentTheme == 4 ? &snd().snd_saturn_launch : &snd().snd_launch); }
void playBack() { mmEffectEx(currentTheme == 4 ? &snd().snd_saturn_back : &snd().snd_back); }

// Word-wraps text for the small font, keeping existing line breaks
std::string wrap(const std::string &text, int width) {
	std::string out, line;
	size_t start = 0;
	while (start <= text.size()) {
		size_t nl = text.find('\n', start);
		std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
		line.clear();
		size_t p = 0;
		while (p <= para.size()) {
			size_t sp = para.find(' ', p);
			std::string word = para.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
			std::string candidate = line.empty() ? word : line + " " + word;
			if (!line.empty() && calcSmallFontWidth(candidate) > width) {
				out += line + "\n";
				line = word;
			} else {
				line = candidate;
			}
			if (sp == std::string::npos)
				break;
			p = sp + 1;
		}
		out += line;
		if (nl == std::string::npos)
			break;
		out += "\n";
		start = nl + 1;
	}
	return out;
}

int countLines(const std::string &s) {
	int n = 1;
	for (char c : s)
		if (c == '\n')
			n++;
	return n;
}

// Shows a title, a text and a list of choices on the bottom screen.
// Returns the chosen index, or -1 if B was pressed.
int installScreen(const std::string &title, const std::string &body, const std::vector<std::string> &options, int cursor = 0) {
	const bool rtl = ms().rtl();
	const int x = rtl ? 256 - 4 : 4;
	const Alignment align = rtl ? Alignment::right : Alignment::left;
	const std::string text = wrap(body, 244);
	int optionsY = 26 + countLines(text) * smallFontHeight() + 6;
	int maxY = 190 - (int)options.size() * 15;
	if (optionsY > maxY)
		optionsY = maxY;

	bool refresh = true;
	while (1) {
		if (refresh) {
			clearText();
			printLarge(false, x, 0, title, align);
			printSmall(false, x, 24, text, align);
			for (size_t i = 0; i < options.size(); i++) {
				bool sel = (int)i == cursor;
				printSmall(false, rtl ? 256 - 16 : 16, optionsY + i * 15, options[i], align,
				           (sel && currentTheme != 4) ? FontPalette::user : FontPalette::regular);
				if (sel)
					printSmall(false, rtl ? 256 - 6 : 6, optionsY + i * 15, rtl ? "<" : ">", align);
			}
			updateText(false);
			updateText(true);
			refresh = false;
		}

		if (!gui().isExited())
			snd().playBgMusic(ms().settingsMusic);

		do {
			scanKeys();
			pressed = keysDownRepeat();
			touchRead(&touch);
			swiWaitForVBlank();
		} while (!pressed);

		if ((pressed & KEY_UP) && options.size() > 1) {
			playSelect();
			cursor = (cursor + options.size() - 1) % options.size();
			refresh = true;
		} else if ((pressed & KEY_DOWN) && options.size() > 1) {
			playSelect();
			cursor = (cursor + 1) % options.size();
			refresh = true;
		} else if (pressed & KEY_A) {
			playLaunch();
			clearText();
			return cursor;
		} else if (pressed & KEY_B) {
			playBack();
			clearText();
			return -1;
		} else if (pressed & KEY_TOUCH) {
			for (size_t i = 0; i < options.size(); i++) {
				if (touch.py >= optionsY + (int)i * 15 && touch.py < optionsY + (int)i * 15 + 15) {
					playLaunch();
					clearText();
					return (int)i;
				}
			}
		}
	}
}

void message(const std::string &title, const std::string &body) {
	installScreen(title, body, {STR_OK});
}

[[noreturn]] void launch(const std::string &path, const std::vector<std::string> &args) {
	std::vector<const char *> argv;
	for (const std::string &a : args)
		argv.push_back(a.c_str());
	int err = runNdsFile(path.c_str(), argv.size(), argv.data(), sys().isRunFromSD(), true, true, false, true, true, false, -1, 0);
	char text[64];
	snprintf(text, sizeof(text), STR_INSTALL_START_FAILED.c_str(), err);
	message(STR_INSTALLATION_SETTINGS, text);
	while (1)
		swiWaitForVBlank();
}

// Restarts TWiLight Menu++ without saving the settings in memory
[[noreturn]] void restartTwilight() {
	std::string path = std::string(root()) + "/_nds/TWiLightMenu/main.srldr";
	launch(path, {path});
}

bool openUnlaunchInstaller(const std::string &path) {
	int c = installScreen(STR_INSTALL_WARNING, STR_INSTALL_CONFIRM_INSTALLER, {STR_NO, STR_YES});
	if (c != 1)
		return false;
	launch(path, {path});
}

} // namespace

bool installSettingsBackupFound(void) { return exists(settingsBackupPath()); }

bool installHubFound(void) { return exists(std::string(root()) + "/_nds/TWiLightMenu/hub.srldr"); }

void opt_install_status(void) {
	BootMethod m = bootMethod();
	std::string s;
	s += STR_INSTALL_BOOT_METHOD + " ";
	switch (m) {
		case BOOT_FLASHCARD:
			s += STR_INSTALL_METHOD_FLASHCARD;
			break;
		case BOOT_3DS:
			s += STR_INSTALL_METHOD_3DS;
			break;
		case BOOT_FULL:
			s += hiyaFound() ? STR_INSTALL_METHOD_HIYA : STR_INSTALL_METHOD_UNLAUNCH;
			break;
		case BOOT_EXPLOIT:
			s += STR_INSTALL_METHOD_EXPLOIT;
			break;
	}
	s += "\n" + STR_INSTALL_AUTOSTART + " ";
	switch (m) {
		case BOOT_FULL:
			s += hiyaFound() ? (exists("sd:/hiya/autoboot.bin") ? STR_YES : STR_NO) : STR_INSTALL_AUTOSTART_UNLAUNCH;
			break;
		case BOOT_EXPLOIT:
			s += STR_NO;
			break;
		default:
			s += STR_INSTALL_AUTOSTART_UNKNOWN;
			break;
	}
	if (isDSiMode() && sys().isRunFromSD()) {
		s += "\nBOOT.NDS: " + (exists("sd:/BOOT.NDS") ? STR_INSTALL_FOUND : STR_INSTALL_NOT_FOUND);
		s += "\n" + STR_INSTALL_UNLAUNCH_INSTALLER + " " + (unlaunchInstaller().empty() ? STR_INSTALL_NOT_FOUND : STR_INSTALL_FOUND);
	}
	s += "\n" + STR_INSTALL_SETTINGS_BACKUP + " " + (installSettingsBackupFound() ? STR_YES : STR_NO);
	message(STR_INSTALL_STATUS, s);
}

void opt_install_permanent(void) {
	switch (bootMethod()) {
		case BOOT_FLASHCARD:
			message(STR_MAKE_PERMANENT, STR_INSTALL_PERM_FLASHCARD);
			return;
		case BOOT_3DS:
			if (exists("sd:/luma/config.ini")) {
				if (installScreen(STR_MAKE_PERMANENT, STR_INSTALL_PERM_3DS, {STR_SET_LUMA_AUTOBOOT, STR_NO}) == 0)
					opt_set_luma_autoboot();
			} else {
				message(STR_MAKE_PERMANENT, STR_INSTALL_PERM_3DS);
			}
			return;
		case BOOT_FULL: {
			std::string text;
			if (hiyaFound()) {
				if (installScreen(STR_MAKE_PERMANENT, STR_INSTALL_PERM_HIYA, {STR_INSTALL_ENABLE_AUTOSTART, STR_NO}) != 0)
					return;
				opt_hiya_autoboot_toggle(false, true);
				hiyaAutobootFound = true;
				message(STR_DONE, STR_INSTALL_PERM_HIYA_DONE);
				return;
			}
			text = STR_INSTALL_PERM_UNLAUNCH;
			if (!exists("sd:/BOOT.NDS"))
				text += "\n\n" + STR_INSTALL_MISSING_BOOTNDS;
			message(STR_MAKE_PERMANENT, text);
			return;
		}
		case BOOT_EXPLOIT: {
			std::string installer = unlaunchInstaller();
			if (installer.empty()) {
				message(STR_MAKE_PERMANENT, STR_INSTALL_PERM_EXPLOIT + "\n\n" + STR_INSTALL_NO_INSTALLER);
				return;
			}
			if (installScreen(STR_MAKE_PERMANENT, STR_INSTALL_PERM_EXPLOIT, {STR_INSTALL_OPEN_INSTALLER, STR_NO}) == 0)
				openUnlaunchInstaller(installer);
			return;
		}
	}
}

void opt_install_restore(void) {
	BootMethod m = bootMethod();
	if (installScreen(STR_RESTORE_ORIGINAL, STR_INSTALL_RESTORE_INFO, {STR_NO, STR_INSTALL_RESTORE}) != 1)
		return;

	// 1. hiyaCFW: boot the DSi Menu again
	if (exists("sd:/hiya/autoboot.bin")) {
		opt_hiya_autoboot_toggle(true, false);
		hiyaAutobootFound = false;
	}

	// 2. Back up the settings and let TWiLight Menu++ start from scratch
	remove(settingsBackupPath().c_str());
	rename(settingsIniPath().c_str(), settingsBackupPath().c_str());

	// 3. What only the user (or the Unlaunch installer) can undo
	if (m == BOOT_3DS) {
		message(STR_RESTORE_ORIGINAL, STR_LUMA_AUTOBOOT_REVERT);
	} else if (m == BOOT_FULL) {
		std::string installer = unlaunchInstaller();
		if (installer.empty()) {
			message(STR_RESTORE_ORIGINAL, STR_INSTALL_RESTORE_UNLAUNCH + "\n\n" + STR_INSTALL_NO_INSTALLER);
		} else if (installScreen(STR_RESTORE_ORIGINAL, STR_INSTALL_RESTORE_UNLAUNCH, {STR_INSTALL_NOT_NOW, STR_INSTALL_OPEN_INSTALLER}) == 1) {
			openUnlaunchInstaller(installer);
		}
	}

	message(STR_DONE, STR_INSTALL_RESTORED);
	restartTwilight();
}

void opt_install_restore_backup(void) {
	if (!installSettingsBackupFound()) {
		message(STR_RESTORE_SETTINGS_BACKUP, STR_INSTALL_NO_BACKUP);
		return;
	}
	if (installScreen(STR_RESTORE_SETTINGS_BACKUP, STR_DESCRIPTION_RESTORE_SETTINGS_BACKUP, {STR_NO, STR_YES}) != 1)
		return;
	remove(settingsIniPath().c_str());
	rename(settingsBackupPath().c_str(), settingsIniPath().c_str());
	message(STR_DONE, STR_INSTALL_BACKUP_RESTORED);
	restartTwilight();
}

static void launchHub(void) {
	std::string hub = std::string(root()) + "/_nds/TWiLightMenu/hub.srldr";
	std::string settings = std::string(root()) + "/_nds/TWiLightMenu/settings.srldr";
	std::vector<const char *> argv = {hub.c_str(), settings.c_str()};
	runNdsFile(hub.c_str(), argv.size(), argv.data(), sys().isRunFromSD(), true, false, false, true, true, false, -1, sys().commonCache());
}

void opt_open_hub(void) {
	gui().onExit(launchHub).saveAndExit(currentTheme);
}

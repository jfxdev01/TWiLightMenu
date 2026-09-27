// TWiLight Hub - home screen
//
// A launcher for small DSi utilities (camera, photo album, web browser, WiFi,
// weather, clock sync, box art downloader), with a look inspired by the
// current Nintendo system menus: a row of big tiles, round shortcut buttons
// and button hints at the bottom.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "apps.h"
#include "config.h"
#include "font.h"
#include "i18n.h"
#include "net.h"
#include "platform.h"
#include "ui.h"

namespace apps {

static const AppInfo kApps[] = {
	{camera, ICON_CAMERA, HEXCOLOR(0xFF5A46), "Camera", "Câmera",
	 "Take photos with the inner and outer cameras. Photos are saved as PNG in /DCIM.",
	 "Tire fotos com a câmera interna e a externa. As fotos são salvas em PNG na pasta /DCIM.", true},
	{album, ICON_ALBUM, HEXCOLOR(0x2D8CFF), "Album", "Álbum",
	 "View the photos taken with the Hub and the DSi Camera.",
	 "Veja as fotos tiradas com o Hub e com a Câmera do DSi.", false},
	{[] { browser(); }, ICON_GLOBE, HEXCOLOR(0x00AFC8), "Browser", "Navegador",
	 "A lightweight web browser with HTTPS support, search and bookmarks.",
	 "Um navegador leve com suporte a HTTPS, pesquisa e favoritos.", false},
	{wifi, ICON_WIFI, HEXCOLOR(0x32B45A), "Wi-Fi", "Wi-Fi",
	 "Find networks and connect, including WPA2 networks on DSi. Passwords are remembered.",
	 "Encontre redes e conecte-se, incluindo redes WPA2 no DSi. As senhas ficam salvas.", false},
	{weather, ICON_WEATHER, HEXCOLOR(0xFFA01E), "Weather", "Clima",
	 "Current weather and forecast for your city.",
	 "Tempo atual e previsão para a sua cidade.", false},
	{clock, ICON_CLOCK, HEXCOLOR(0x7D5AD2), "Clock", "Relógio",
	 "Set the console clock automatically from the internet (NTP).",
	 "Acerte o relógio do console automaticamente pela internet (NTP).", false},
	{boxart, ICON_BOXART, HEXCOLOR(0xE63C78), "Box Art", "Capas",
	 "Download the box art of your games for TWiLight Menu++.",
	 "Baixe as capas dos seus jogos para o TWiLight Menu++.", false},
	{system, ICON_INFO, HEXCOLOR(0x64788C), "System", "Sistema",
	 "Console, storage, battery and network information.",
	 "Informações do console, armazenamento, bateria e rede.", false},
};

const AppInfo *list(int &count) {
	count = sizeof(kApps) / sizeof(kApps[0]);
	return kApps;
}

} // namespace apps

namespace {

enum RoundAction { R_THEME, R_LANGUAGE, R_SETTINGS, R_INSTALL, R_EXIT, R_COUNT };

struct RoundInfo {
	IconId icon;
	const char *nameEn, *namePt, *descEn, *descPt;
};

const RoundInfo kRound[R_COUNT] = {
	{ICON_MOON, "Theme", "Tema", "Switch between the dark and the light theme.", "Alterne entre o tema escuro e o claro."},
	{ICON_COUNT, "Language", "Idioma", "Português or English.", "Português ou inglês."},
	{ICON_GEAR, "TWiLight Settings", "Configurações", "Open the TWiLight Menu++ settings.", "Abrir as configurações do TWiLight Menu++."},
	{ICON_TOOLS, "Installation", "Instalação", "Make TWiLight Menu++ start automatically, or restore the console to how it was.",
	 "Faça o TWiLight Menu++ iniciar automaticamente ou restaure o console ao estado original."},
	{ICON_POWER, "Back to menu", "Voltar ao menu", "Close the Hub and return to TWiLight Menu++.", "Fechar o Hub e voltar ao TWiLight Menu++."},
};

const int kTile = 64, kGap = 12, kTileY = 44;

std::string appName(const apps::AppInfo &a) { return TR(a.nameEn, a.namePt); }

std::string contextLine(int index) {
	char buf[96];
	switch (index) {
		case 0:
			return platform::isDSi() ? TR("Inner and outer camera", "Câmera interna e externa")
			                         : TR("Needs a Nintendo DSi in DSi mode", "Requer um Nintendo DSi no modo DSi");
		case 5: {
			time_t t = platform::now();
			struct tm *tm = localtime(&t);
			snprintf(buf, sizeof(buf), "%02d/%02d/%04d  %02d:%02d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900, tm->tm_hour, tm->tm_min);
			return std::string(TR("Console time: ", "Hora do console: ")) + buf;
		}
		case 7:
			snprintf(buf, sizeof(buf), "%s  ·  %d%%", platform::consoleName().c_str(), platform::batteryLevel());
			return buf;
		case 1:
			return TR("Photos from /DCIM", "Fotos da pasta /DCIM");
		default:
			if (!net::initialized())
				return TR("Wi-Fi connects when needed", "O Wi-Fi conecta quando necessário");
			return net::stateText();
	}
}

void launchTwlSettings(bool installPage) {
	std::string path = platform::twlDir() + "/settings.srldr";
	std::vector<std::string> args = {path};
	if (installPage)
		args.push_back("install");
	platform::setBrightness(3, 16);
	if (!platform::chainload(path, args)) {
		platform::setBrightness(3, 0);
		ui::message(TR("Settings", "Configurações"),
		            TR("Couldn't open the TWiLight Menu++ settings from here. Open them from the menu instead.",
		               "Não foi possível abrir as configurações do TWiLight Menu++ daqui. Abra-as pelo menu."));
	}
}

void fadeIn() {
	for (int i = 16; i >= 0; i -= 2) {
		platform::setBrightness(3, i);
		platform::waitVBlank();
	}
}

} // namespace

int main(int argc, char **argv) {
	platform::init(argc, argv);
	platform::setBrightness(3, 16);
	config::load();
	i18n::init();
	ui::applyTheme(config::darkTheme());
	fonts::init();
	ui::initIcons();

	int count;
	const apps::AppInfo *list = apps::list(count);

	int sel = config::ini().getInt("HUB", "LAST_APP", 0);
	if (sel < 0 || sel >= count)
		sel = 0;
	int row = 0, roundSel = 0;
	int scroll = 0;
	bool first = true;

	while (true) {
		platform::scanInput();
		net::update();

		Surface &top = platform::top();
		Surface &bottom = platform::bottom();
		const ui::Palette &p = ui::pal();

		// --- Bottom screen: tiles, round buttons, footer ---
		ui::background(bottom);
		int target = sel * (kTile + kGap) - (256 - kTile) / 2 + 16;
		int maxScroll = count * (kTile + kGap) - kGap + 32 - 256;
		if (target > maxScroll)
			target = maxScroll;
		if (target < 0)
			target = 0;
		if (first)
			scroll = target;
		scroll += (target - scroll) / 3 + ((target > scroll) ? 1 : (target < scroll ? -1 : 0));
		if (abs(target - scroll) <= 1)
			scroll = target;

		std::vector<ui::Rect> tileRects;
		for (int i = 0; i < count; i++) {
			ui::Rect r{16 + i * (kTile + kGap) - scroll, kTileY, kTile, kTile};
			tileRects.push_back(r);
			if (r.x > 256 || r.x + r.w < 0)
				continue;
			if (!p.dark)
				gfx::shadow(bottom, r.x, r.y, r.w, r.h, 12, 3, 70);
			ui::appTile(bottom, r, list[i].icon, list[i].color, false);
			if (list[i].needsDSi && !platform::isDSi())
				gfx::fillRoundRect(bottom, r.x, r.y, r.w, r.h, 10, p.bg, 150);
		}
		if (row == 0) {
			ui::Rect &r = tileRects[sel];
			ui::cursorFrame(bottom, r, 10);
			// Selected app name above the tile, like the Switch
			std::string name = appName(list[sel]);
			int w = fonts::bold.width(name);
			int x = r.x + r.w / 2 - w / 2;
			if (x < 6)
				x = 6;
			if (x + w > 250)
				x = 250 - w;
			fonts::bold.draw(bottom, x, 16, name, p.accent);
		}

		std::vector<ui::Rect> roundRects;
		for (int i = 0; i < R_COUNT; i++) {
			int cx = 128 + (i - 2) * 46, cy = 138;
			roundRects.push_back({cx - 18, cy - 18, 36, 36});
			IconId ic = kRound[i].icon;
			if (i == R_THEME)
				ic = p.dark ? ICON_MOON : ICON_SUN;
			uint16_t glyph = i == R_EXIT ? p.danger : (i == R_INSTALL ? p.accent : p.text);
			ui::roundButton(bottom, cx, cy, 17, ic, row == 1 && roundSel == i, glyph,
			                i == R_LANGUAGE ? (i18n::isPt() ? "PT" : "EN") : "");
		}
		if (row == 1) {
			std::string name = TR(kRound[roundSel].nameEn, kRound[roundSel].namePt);
			fonts::bold.drawCentered(bottom, 128, 16, name, p.accent);
		}
		ui::footer(bottom, {{"+", TR("Menu", "Menu"), KEY_START}, {"A", TR("Open", "Abrir"), KEY_A}}, "TWiLight Hub");

		// --- Top screen: selected item details ---
		ui::background(top);
		ui::statusBar(top);
		if (row == 0) {
			const apps::AppInfo &a = list[sel];
			ui::appTile(top, {16, 44, 80, 80}, a.icon, a.color, true);
			fonts::head.drawEllipsized(top, 108, 46, appName(a), 140, p.text);
			fonts::body.drawWrapped(top, 108, 74, TR(a.descEn, a.descPt), 140, p.textDim, 5);
			ui::card(top, {12, 142, 232, 32}, 10);
			fonts::body.drawEllipsized(top, 22, 158 - fonts::body.lineHeight() / 2, contextLine(sel), 212, p.text);
		} else {
			const RoundInfo &r = kRound[roundSel];
			IconId ic = roundSel == R_THEME ? (p.dark ? ICON_MOON : ICON_SUN) : (roundSel == R_LANGUAGE ? ICON_GLOBE : r.icon);
			ui::appTile(top, {16, 44, 80, 80}, ic, roundSel == R_EXIT ? p.danger : HEXCOLOR(0x5A6E82), true);
			fonts::head.drawEllipsized(top, 108, 46, TR(r.nameEn, r.namePt), 140, p.text);
			fonts::body.drawWrapped(top, 108, 74, TR(r.descEn, r.descPt), 140, p.textDim, 5);
		}
		ui::drawToast(top);

		// --- Input ---
		uint32_t kd = platform::keysDown() | ui::footerTapped();
		uint32_t kr = platform::keysRepeat();
		bool activate = false;

		for (int i = 0; i < count; i++) {
			if (ui::tapped(tileRects[i])) {
				if (row == 0 && sel == i) {
					activate = true;
				} else {
					row = 0;
					sel = i;
					platform::playSfx(platform::SFX_MOVE);
				}
			}
		}
		for (int i = 0; i < R_COUNT; i++) {
			if (ui::tapped(roundRects[i])) {
				row = 1;
				roundSel = i;
				activate = true;
			}
		}

		if (kr & KEY_LEFT) {
			if (row == 0 && sel > 0)
				sel--;
			else if (row == 1 && roundSel > 0)
				roundSel--;
			platform::playSfx(platform::SFX_MOVE);
		}
		if (kr & KEY_RIGHT) {
			if (row == 0 && sel < count - 1)
				sel++;
			else if (row == 1 && roundSel < R_COUNT - 1)
				roundSel++;
			platform::playSfx(platform::SFX_MOVE);
		}
		if (kd & (KEY_UP | KEY_DOWN)) {
			row ^= 1;
			platform::playSfx(platform::SFX_MOVE);
		}
		if (kd & KEY_L)
			sel = 0;
		if (kd & KEY_R)
			sel = count - 1;
		if (kd & KEY_A)
			activate = true;
		if (kd & KEY_START) {
			config::ini().setInt("HUB", "LAST_APP", sel);
			config::save();
			platform::playSfx(platform::SFX_BACK);
			platform::exitToMenu();
		}

		if (activate) {
			if (row == 0) {
				const apps::AppInfo &a = list[sel];
				if (a.needsDSi && !platform::isDSi()) {
					platform::playSfx(platform::SFX_ERROR);
					ui::toast(TR("This app needs a Nintendo DSi", "Este app requer um Nintendo DSi"));
				} else {
					platform::playSfx(platform::SFX_SELECT);
					config::ini().setInt("HUB", "LAST_APP", sel);
					config::save();
					platform::present();
					a.run();
				}
			} else {
				platform::playSfx(platform::SFX_SELECT);
				switch (roundSel) {
					case R_THEME:
						config::setDarkTheme(!ui::pal().dark);
						ui::applyTheme(config::darkTheme());
						config::save();
						break;
					case R_LANGUAGE:
						i18n::setPt(!i18n::isPt());
						config::save();
						break;
					case R_SETTINGS:
						config::save();
						launchTwlSettings(false);
						break;
					case R_INSTALL:
						config::save();
						launchTwlSettings(true);
						break;
					case R_EXIT:
						config::ini().setInt("HUB", "LAST_APP", sel);
						config::save();
						platform::exitToMenu();
				}
			}
		}

		platform::present();
		if (first) {
			fadeIn();
			first = false;
		}
	}
	return 0;
}

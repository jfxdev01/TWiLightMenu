// System: console, storage, battery and network information, Hub settings
#include <stdio.h>

#include "apps.h"
#include "config.h"
#include "i18n.h"
#include "net.h"
#include "platform.h"
#include "ui.h"

#ifndef HUB_VERSION
#define HUB_VERSION "1.0"
#endif

namespace apps {

namespace {

const uint16_t kColor = HEXCOLOR(0x64788C);

std::string sizeText(uint64_t bytes) {
	char buf[32];
	if (bytes >= (1ULL << 30))
		snprintf(buf, sizeof(buf), "%u.%u GB", (unsigned)(bytes >> 30), (unsigned)(((bytes >> 20) & 1023) * 10 / 1024));
	else
		snprintf(buf, sizeof(buf), "%u MB", (unsigned)(bytes >> 20));
	return buf;
}

} // namespace

void system() {
	ui::ListView list;
	list.area = {0, 0, 256, 168};
	list.itemHeight = 30;
	uint64_t freeB = 0, totalB = 0;
	bool haveSpace = platform::sdFreeSpace(freeB, totalB);

	while (true) {
		platform::scanInput();
		net::update();
		const ui::Palette &p = ui::pal();
		Surface &top = platform::top();
		Surface &bottom = platform::bottom();

		// Top: console summary
		ui::background(top);
		ui::statusBar(top);
		int y = ui::titleBar(top, ICON_INFO, kColor, platform::consoleName(), std::string(TR("Nickname: ", "Apelido: ")) + platform::nickname());
		ui::card(top, {10, y, 236, 100}, 10);
		// Storage bar
		fonts::bold.draw(top, 20, y + 8, TR("Storage", "Armazenamento"), p.text);
		if (haveSpace && totalB > 0) {
			int used = (int)((totalB - freeB) * 100 / totalB);
			ui::progressBar(top, {20, y + 28, 216, 8}, used);
			std::string s = sizeText(freeB) + TR(" free of ", " livres de ") + sizeText(totalB);
			fonts::body.draw(top, 20, y + 40, s, p.textDim);
		} else {
			fonts::body.draw(top, 20, y + 28, TR("Unknown", "Desconhecido"), p.textDim);
		}
		fonts::bold.draw(top, 20, y + 60, TR("Battery", "Bateria"), p.text);
		ui::battery(top, 20, y + 80, platform::batteryLevel(), platform::charging(), p.text);
		char bat[48];
		snprintf(bat, sizeof(bat), "%d%%%s", platform::batteryLevel(), platform::charging() ? TR(" · charging", " · carregando") : "");
		fonts::body.draw(top, 48, y + 78, bat, p.textDim);
		ui::drawToast(top);

		// Bottom: details and settings
		list.items.clear();
		auto add = [&](const std::string &t, const std::string &r, IconId ic = ICON_COUNT) {
			ui::ListItem it;
			it.title = t;
			it.right = r;
			it.icon = ic;
			list.items.push_back(it);
		};
		add(TR("Sound effects", "Efeitos sonoros"), config::soundEffects() ? TR("On", "Ligado") : TR("Off", "Desligado"), ICON_STAR);
		add(TR("Theme", "Tema"), p.dark ? TR("Dark", "Escuro") : TR("Light", "Claro"), p.dark ? ICON_MOON : ICON_SUN);
		add(TR("Language", "Idioma"), i18n::isPt() ? "Português" : "English", ICON_GLOBE);
		add(TR("Wi-Fi mode", "Modo Wi-Fi"), config::dsiWifi() ? "DSi (WPA2)" : "DS (WEP)", ICON_WIFI);
		add(TR("IP address", "Endereço IP"), net::state() == net::STATE_CONNECTED ? net::ipAddress() : "—");
		add(TR("MAC address", "Endereço MAC"), net::initialized() ? net::macAddress() : "—");
		add(TR("Running in", "Executando em"), platform::isDSi() ? TR("DSi mode", "modo DSi") : TR("DS mode", "modo DS"));
		add(TR("TWiLight Menu++ folder", "Pasta do TWiLight Menu++"), platform::twlDir());
		add(TR("TWiLight Hub version", "Versão do TWiLight Hub"), HUB_VERSION);
		ui::background(bottom);
		int act = list.update();
		list.draw(bottom);
		ui::footer(bottom, {{"B", TR("Back", "Voltar"), KEY_B}, {"A", TR("Change", "Alterar"), KEY_A}});

		uint32_t kd = platform::keysDown() | ui::footerTapped();
		switch (act) {
			case 0:
				config::setSoundEffects(!config::soundEffects());
				config::save();
				platform::playSfx(platform::SFX_TOGGLE);
				break;
			case 1:
				config::setDarkTheme(!p.dark);
				ui::applyTheme(config::darkTheme());
				config::save();
				break;
			case 2:
				i18n::setPt(!i18n::isPt());
				config::save();
				break;
			case 3:
				config::setDsiWifi(!config::dsiWifi());
				config::save();
				ui::toast(TR("Applies the next time the Hub starts", "Vale na próxima vez que o Hub abrir"));
				break;
			default:
				break;
		}
		if (kd & KEY_B) {
			platform::playSfx(platform::SFX_BACK);
			return;
		}
		platform::present();
	}
}

} // namespace apps

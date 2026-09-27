// Wi-Fi: scan, connect (open/WEP/WPA2), remember passwords
#include <stdio.h>

#include "apps.h"
#include "config.h"
#include "i18n.h"
#include "net.h"
#include "platform.h"
#include "ui.h"

namespace apps {

static const uint16_t kColor = HEXCOLOR(0x32B45A);

static bool connectWithUi(const net::AccessPoint &ap, const std::string &typedPassword) {
	net::connectTo(ap, typedPassword);
	while (true) {
		platform::scanInput();
		net::update();
		if (net::state() == net::STATE_CONNECTED) {
			if (!typedPassword.empty())
				net::savePassword(ap.ssid, typedPassword);
			else if (!ap.secured)
				net::savePassword(ap.ssid, "");
			platform::playSfx(platform::SFX_SELECT);
			ui::toast(net::stateText());
			return true;
		}
		if (net::state() == net::STATE_FAILED) {
			platform::playSfx(platform::SFX_ERROR);
			ui::message(TR("Couldn't connect", "Não foi possível conectar"),
			            ap.secured ? TR("Check the password and try again. On DSi, WPA2 networks need the DSi mode (Y).",
			                            "Verifique a senha e tente de novo. No DSi, redes WPA2 precisam do modo DSi (Y).")
			                       : TR("The network didn't answer.", "A rede não respondeu."));
			return false;
		}
		ui::busy(TR("Connecting", "Conectando"), ap.ssid + "\n" + net::stateText(), -1, true);
		if ((platform::keysDown() | ui::footerTapped()) & KEY_B) {
			net::disconnect();
			return false;
		}
	}
}

void wifi() {
	if (!net::init()) {
		ui::message("Wi-Fi", net::initError());
		return;
	}
	ui::ListView list;
	list.area = {0, 30, 256, 138};
	std::vector<net::AccessPoint> aps;
	bool scanning = net::state() != net::STATE_CONNECTED;
	if (scanning)
		net::startScan();
	uint32_t lastRefresh = 0;

	while (true) {
		platform::scanInput();
		net::update();

		if (scanning && platform::frame() - lastRefresh > 45) {
			lastRefresh = platform::frame();
			std::string selSsid = list.selected < (int)aps.size() ? aps[list.selected].ssid : "";
			aps = net::scanResults();
			// Strongest first
			for (size_t i = 0; i < aps.size(); i++)
				for (size_t j = i + 1; j < aps.size(); j++)
					if (aps[j].rssi > aps[i].rssi)
						std::swap(aps[i], aps[j]);
			list.items.clear();
			for (size_t i = 0; i < aps.size(); i++) {
				const net::AccessPoint &a = aps[i];
				ui::ListItem it;
				it.title = a.ssid;
				std::string sub = a.security;
				if (!a.supported)
					sub += TR(" · not supported", " · não suportada");
				else if (a.saved)
					sub += TR(" · saved", " · salva");
				it.subtitle = sub;
				it.icon = a.secured ? ICON_LOCK : ICON_WIFI;
				it.enabled = a.supported;
				it.bars = a.bars;
				list.items.push_back(it);
				if (a.ssid == selSsid)
					list.selected = (int)i;
			}
			if (list.selected >= (int)list.items.size())
				list.selected = 0;
		}

		// Top screen
		Surface &top = platform::top();
		const ui::Palette &p = ui::pal();
		ui::background(top);
		ui::statusBar(top);
		int y = ui::titleBar(top, ICON_WIFI, kColor, "Wi-Fi", net::stateText());
		ui::card(top, {10, y, 236, 100}, 10);
		struct Row {
			const char *label;
			std::string value;
		};
		std::vector<Row> rows = {
			{TR("Network", "Rede"), net::state() == net::STATE_CONNECTED ? net::ssid() : "—"},
			{TR("IP address", "Endereço IP"), net::state() == net::STATE_CONNECTED ? net::ipAddress() : "—"},
			{TR("Mode", "Modo"), net::dsiMode() ? "DSi (WPA2)" : "DS (WEP)"},
			{TR("MAC address", "Endereço MAC"), net::macAddress()},
		};
		for (size_t i = 0; i < rows.size(); i++) {
			int ry = y + 10 + i * 22;
			fonts::body.draw(top, 22, ry, rows[i].label, p.textDim);
			fonts::bold.drawRight(top, 234, ry, fonts::bold.ellipsize(rows[i].value, 130), p.text);
			if (i + 1 < rows.size())
				gfx::hline(top, 20, ry + 18, 216, p.divider);
		}
		ui::drawToast(top);

		// Bottom screen
		Surface &bottom = platform::bottom();
		ui::background(bottom);
		if (scanning) {
			fonts::bold.draw(bottom, 12, 10, TR("Networks nearby", "Redes próximas"), p.text);
			list.showEmptyText = false;
			if (list.items.empty())
				ui::spinner(bottom, 128, 96, 12);
			int act = list.update();
			list.draw(bottom);
			ui::footer(bottom, {{"B", TR("Back", "Voltar"), KEY_B}, {"Y", net::dsiMode() ? TR("DS mode", "Modo DS") : TR("DSi mode", "Modo DSi"), KEY_Y}, {"A", TR("Connect", "Conectar"), KEY_A}});
			uint32_t kd = platform::keysDown() | ui::footerTapped();
			if (act >= 0 && act < (int)aps.size()) {
				const net::AccessPoint &a = aps[act];
				std::string pw;
				bool go = true;
				if (a.secured && !a.saved) {
					go = ui::keyboard(std::string(TR("Password for ", "Senha de ")) + a.ssid, pw, 64, true, TR("Network password", "Senha da rede"));
				}
				if (go && connectWithUi(a, pw))
					scanning = false;
			}
			if (kd & KEY_Y) {
				config::setDsiWifi(!config::dsiWifi());
				config::save();
				ui::message("Wi-Fi", config::dsiWifi() ? TR("DSi mode will be used the next time the Hub starts. It supports WPA2 networks.",
				                                            "O modo DSi será usado na próxima vez que o Hub abrir. Ele suporta redes WPA2.")
				                                         : TR("DS mode will be used the next time the Hub starts. It only supports open and WEP networks.",
				                                              "O modo DS será usado na próxima vez que o Hub abrir. Ele só suporta redes abertas e WEP."));
			}
			if (kd & KEY_B) {
				platform::playSfx(platform::SFX_BACK);
				return;
			}
		} else {
			// Connected: show the network and actions
			ui::appTile(bottom, {96, 22, 64, 64}, ICON_WIFI, kColor, false);
			fonts::title.drawCentered(bottom, 128, 94, fonts::title.ellipsize(net::ssid(), 230), p.text);
			fonts::body.drawCentered(bottom, 128, 114, net::ipAddress(), p.textDim);
			ui::Rect rs{16, 136, 108, 26}, rf{132, 136, 108, 26};
			ui::button(bottom, rs, TR("Other networks", "Outras redes"), false, ICON_SEARCH);
			ui::button(bottom, rf, TR("Forget", "Esquecer"), false, ICON_TRASH);
			ui::footer(bottom, {{"B", TR("Back", "Voltar"), KEY_B}, {"X", TR("Forget", "Esquecer"), KEY_X}, {"Y", TR("Search", "Procurar"), KEY_Y}});
			uint32_t kd = platform::keysDown() | ui::footerTapped();
			if ((kd & KEY_Y) || ui::tapped(rs)) {
				net::disconnect();
				net::startScan();
				scanning = true;
				lastRefresh = 0;
			}
			if ((kd & KEY_X) || ui::tapped(rf)) {
				if (ui::confirm(TR("Forget network?", "Esquecer rede?"), net::ssid(), TR("Forget", "Esquecer"), TR("Cancel", "Cancelar"))) {
					net::forget(net::ssid());
					net::disconnect();
					net::startScan();
					scanning = true;
					lastRefresh = 0;
				}
			}
			if (kd & KEY_B) {
				platform::playSfx(platform::SFX_BACK);
				return;
			}
		}
		platform::present();
	}
}

} // namespace apps

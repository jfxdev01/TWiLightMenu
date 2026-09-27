// Box art: downloads the covers of the DS/DSi games on the SD card from
// GameTDB into /_nds/TWiLightMenu/boxart, where TWiLight Menu++ shows them.
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apps.h"
#include "config.h"
#include "i18n.h"
#include "net.h"
#include "platform.h"
#include "ui.h"

namespace apps {

namespace {

const uint16_t kColor = HEXCOLOR(0xE63C78);

struct Game {
	std::string path, name, code, title;
	int state; // 0 pending, 1 downloaded, 2 not found, 3 already had it
};

bool isRom(const char *name) {
	const char *dot = strrchr(name, '.');
	return dot && (strcasecmp(dot, ".nds") == 0 || strcasecmp(dot, ".dsi") == 0 || strcasecmp(dot, ".srl") == 0);
}

bool validCode(const char *c) {
	if (memcmp(c, "####", 4) == 0 || memcmp(c, "\0\0\0\0", 4) == 0)
		return false;
	for (int i = 0; i < 4; i++)
		if (!isupper((unsigned char)c[i]) && !isdigit((unsigned char)c[i]))
			return false;
	return true;
}

void scan(const std::string &dir, int depth, std::vector<Game> &out, int &visited) {
	if (depth > 4 || out.size() > 3000)
		return;
	DIR *d = opendir(dir.c_str());
	if (!d)
		return;
	struct dirent *e;
	std::vector<std::string> subdirs;
	while ((e = readdir(d)) != nullptr) {
		if (e->d_name[0] == '.' || e->d_name[0] == '_')
			continue;
		std::string full = dir + (dir.back() == '/' ? "" : "/") + e->d_name;
		if (e->d_type == DT_DIR) {
			if (strcasecmp(e->d_name, "DCIM") != 0 && strcasecmp(e->d_name, "private") != 0 && strcasecmp(e->d_name, "title") != 0)
				subdirs.push_back(full);
			continue;
		}
		if (!isRom(e->d_name))
			continue;
		if (++visited % 16 == 0)
			ui::busy(TR("Box Art", "Capas"), std::string(TR("Looking for games… ", "Procurando jogos… ")) + std::to_string(out.size()), -1, false);
		FILE *f = fopen(full.c_str(), "rb");
		if (!f)
			continue;
		char hdr[16];
		size_t n = fread(hdr, 1, sizeof(hdr), f);
		fclose(f);
		if (n != sizeof(hdr) || !validCode(hdr + 12))
			continue;
		Game g;
		g.path = full;
		g.name = e->d_name;
		g.code = std::string(hdr + 12, 4);
		g.title = std::string(hdr, strnlen(hdr, 12));
		g.state = 0;
		bool dup = false;
		for (const Game &o : out)
			if (o.code == g.code)
				dup = true;
		if (!dup)
			out.push_back(g);
	}
	closedir(d);
	for (const std::string &s : subdirs)
		scan(s, depth + 1, out, visited);
}

std::vector<std::string> regionsFor(char r) {
	std::vector<std::string> out;
	switch (r) {
		case 'E':
		case 'O':
		case 'T':
			out = {"US", "EN"};
			break;
		case 'J':
			out = {"JA"};
			break;
		case 'K':
			out = {"KO"};
			break;
		case 'D':
			out = {"DE", "EN"};
			break;
		case 'F':
			out = {"FR", "EN"};
			break;
		case 'I':
			out = {"IT", "EN"};
			break;
		case 'S':
			out = {"ES", "EN"};
			break;
		case 'H':
			out = {"NL", "EN"};
			break;
		case 'U':
			out = {"AU", "EN"};
			break;
		default:
			out = {"EN"};
	}
	for (const char *extra : {"EN", "US", "JA"}) {
		bool have = false;
		for (auto &o : out)
			if (o == extra)
				have = true;
		if (!have)
			out.push_back(extra);
	}
	return out;
}

bool cancelCheck(void *, size_t, size_t) {
	platform::scanInput();
	return !(platform::keysDown() & KEY_B);
}

} // namespace

void boxart() {
	const ui::Palette &p = ui::pal();
	std::string boxDir = platform::twlDir() + "/boxart";
	std::vector<Game> games;
	int visited = 0;
	for (const std::string &folder : config::romFolders())
		scan(folder, 0, games, visited);
	int missing = 0;
	for (Game &g : games) {
		std::string a = boxDir + "/" + g.code + ".png", b = boxDir + "/" + g.name + ".png";
		if (access(a.c_str(), F_OK) == 0 || access(b.c_str(), F_OK) == 0)
			g.state = 3;
		else
			missing++;
	}

	ui::ListView list;
	list.area = {0, 0, 256, 168};
	list.itemHeight = 32;
	list.emptyText = TR("No DS games found in the ROM folders", "Nenhum jogo de DS nas pastas de ROMs");
	auto rebuild = [&]() {
		list.items.clear();
		for (const Game &g : games) {
			ui::ListItem it;
			it.title = g.title.empty() ? g.name : g.title;
			it.subtitle = g.code + " · " + g.name;
			static const char *kState[2][4] = {{"", "Downloaded", "Not found", "OK"}, {"", "Baixada", "Não achada", "OK"}};
			it.right = kState[i18n::isPt() ? 1 : 0][g.state];
			it.icon = g.state == 1 || g.state == 3 ? ICON_CHECK : (g.state == 2 ? ICON_CLOSE : ICON_BOXART);
			list.items.push_back(it);
		}
	};
	rebuild();
	int downloaded = 0, notFound = 0;
	bool done = false;

	while (true) {
		platform::scanInput();
		Surface &top = platform::top();
		Surface &bottom = platform::bottom();
		ui::background(top);
		ui::statusBar(top);
		char sub[96];
		snprintf(sub, sizeof(sub), TR("%d games, %d without box art", "%d jogos, %d sem capa"), (int)games.size(), missing);
		int y = ui::titleBar(top, ICON_BOXART, kColor, TR("Box Art", "Capas"), sub);
		ui::card(top, {10, y, 236, 96}, 10);
		std::string info = done ? std::string(TR("Finished! ", "Pronto! ")) + std::to_string(downloaded) + TR(" downloaded, ", " baixadas, ") +
		                              std::to_string(notFound) + TR(" not found.", " não encontradas.")
		                        : TR("Covers are downloaded from GameTDB and saved in /_nds/TWiLightMenu/boxart. Turn on \"Box art\" in the TWiLight Menu++ settings to see them.",
		                             "As capas são baixadas do GameTDB e salvas em /_nds/TWiLightMenu/boxart. Ative \"Capas\" nas configurações do TWiLight Menu++ para vê-las.");
		fonts::body.drawWrapped(top, 20, y + 10, info, 216, p.text, 5);
		ui::drawToast(top);

		ui::background(bottom);
		list.update();
		list.draw(bottom);
		std::vector<ui::Hint> hints = {{"B", TR("Back", "Voltar"), KEY_B}};
		if (missing > 0 && !done)
			hints.push_back({"A", TR("Download", "Baixar"), KEY_A});
		ui::footer(bottom, hints);

		uint32_t kd = platform::keysDown() | ui::footerTapped();
		if ((kd & KEY_A) && missing > 0 && !done) {
			if (!net::ensureConnected())
				continue;
			mkdir(boxDir.c_str(), 0777);
			int index = 0;
			bool cancelled = false;
			for (Game &g : games) {
				if (g.state != 0)
					continue;
				index++;
				char progress[96];
				snprintf(progress, sizeof(progress), "%s (%d/%d)\n%s", g.code.c_str(), index, missing, g.title.c_str());
				ui::busy(TR("Downloading box art", "Baixando capas"), progress, index * 100 / missing, true);
				platform::scanInput();
				if (platform::keysDown() & KEY_B) {
					cancelled = true;
					break;
				}
				std::string dest = boxDir + "/" + g.code + ".png";
				bool ok = false;
				for (const std::string &region : regionsFor(g.code[3])) {
					net::Request req;
					req.url = "https://art.gametdb.com/ds/coverS/" + region + "/" + g.code + ".png";
					req.maxBytes = 512 * 1024;
					req.progress = cancelCheck;
					net::Response res;
					if (!net::fetch(req, res)) {
						if (res.error == TR("Cancelled", "Cancelado")) {
							cancelled = true;
							break;
						}
						continue;
					}
					if (res.status == 200 && res.body.size() > 8 && memcmp(res.body.data(), "\x89PNG", 4) == 0) {
						FILE *f = fopen(dest.c_str(), "wb");
						if (f) {
							ok = fwrite(res.body.data(), 1, res.body.size(), f) == res.body.size();
							fclose(f);
							if (!ok)
								remove(dest.c_str());
						}
						break;
					}
				}
				if (cancelled)
					break;
				g.state = ok ? 1 : 2;
				if (ok)
					downloaded++;
				else
					notFound++;
			}
			missing = 0;
			for (const Game &g : games)
				if (g.state == 0)
					missing++;
			done = !cancelled;
			rebuild();
			platform::playSfx(cancelled ? platform::SFX_BACK : platform::SFX_SELECT);
		}
		if (kd & KEY_B) {
			platform::playSfx(platform::SFX_BACK);
			return;
		}
		platform::present();
	}
}

} // namespace apps

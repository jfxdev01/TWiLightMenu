// Album: photos from /DCIM (Hub photos and the DSi Camera's own photos)
#include <algorithm>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "apps.h"
#include "i18n.h"
#include "image.h"
#include "platform.h"
#include "ui.h"

namespace apps {

namespace {

const uint16_t kColor = HEXCOLOR(0x2D8CFF);
const int kCols = 4, kThumbW = 56, kThumbH = 42, kCellW = 62, kCellH = 48, kGridX = 4, kGridY = 6, kRows = 3;

bool isPhoto(const char *name) {
	const char *dot = strrchr(name, '.');
	if (!dot)
		return false;
	return strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".bmp") == 0;
}

std::vector<std::string> findPhotos() {
	std::vector<std::string> out;
	std::string dcim = platform::root() + "/DCIM";
	DIR *d = opendir(dcim.c_str());
	if (!d)
		return out;
	std::vector<std::string> dirs;
	struct dirent *e;
	while ((e = readdir(d)) != nullptr) {
		if (e->d_name[0] == '.')
			continue;
		if (e->d_type == DT_DIR)
			dirs.push_back(dcim + "/" + e->d_name);
		else if (isPhoto(e->d_name))
			out.push_back(dcim + "/" + e->d_name);
	}
	closedir(d);
	for (const std::string &dir : dirs) {
		DIR *sd = opendir(dir.c_str());
		if (!sd)
			continue;
		while ((e = readdir(sd)) != nullptr)
			if (e->d_type != DT_DIR && isPhoto(e->d_name))
				out.push_back(dir + "/" + e->d_name);
		closedir(sd);
	}
	// Newest first (photo numbers grow)
	std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) {
		return a.substr(a.rfind('/')) > b.substr(b.rfind('/'));
	});
	if (out.size() > 400)
		out.resize(400);
	return out;
}

struct ViewerCtx {
	std::vector<std::string> *photos;
	int index;
	bool deleted;
	int move;
};

bool viewerKey(void *user, uint32_t keys) {
	ViewerCtx *c = (ViewerCtx *)user;
	if (keys & KEY_X) {
		std::string name = (*c->photos)[c->index];
		name = name.substr(name.rfind('/') + 1);
		if (ui::confirm(TR("Delete photo?", "Apagar foto?"), name, TR("Delete", "Apagar"), TR("Cancel", "Cancelar"))) {
			remove((*c->photos)[c->index].c_str());
			c->deleted = true;
			return true;
		}
	}
	if (keys & KEY_R) {
		c->move = 1;
		return true;
	}
	if (keys & KEY_L) {
		c->move = -1;
		return true;
	}
	return false;
}

} // namespace

void album() {
	ui::busy(TR("Album", "Álbum"), TR("Looking for photos…", "Procurando fotos…"), -1, false);
	std::vector<std::string> photos = findPhotos();
	std::vector<Image> thumbs(photos.size());
	std::vector<bool> thumbTried(photos.size(), false);
	int sel = 0, scrollRow = 0;
	Image preview;
	int previewIndex = -1;

	while (true) {
		platform::scanInput();
		const ui::Palette &p = ui::pal();
		Surface &top = platform::top();
		Surface &bottom = platform::bottom();
		int n = (int)photos.size();

		// Generate one missing thumbnail per frame, visible ones first
		for (int i = scrollRow * kCols; i < n && i < (scrollRow + kRows) * kCols; i++) {
			if (!thumbTried[i]) {
				thumbTried[i] = true;
				Image full;
				if (image::load(photos[i], full, 320)) {
					Image t;
					image::fit(full, t, kThumbW, kThumbH);
					thumbs[i] = std::move(t);
				}
				break;
			}
		}
		// Bigger preview of the selected photo for the top screen
		if (n > 0 && previewIndex != sel) {
			Image full;
			preview = Image();
			if (image::load(photos[sel], full, 512))
				image::fit(full, preview, 256, 164);
			previewIndex = sel;
		}

		// Top
		ui::background(top);
		ui::statusBar(top);
		if (n == 0) {
			ui::titleBar(top, ICON_ALBUM, kColor, TR("Album", "Álbum"), TR("No photos yet", "Nenhuma foto ainda"));
			fonts::body.drawWrapped(top, 16, 96, TR("Photos taken with the Hub camera and the Nintendo DSi Camera (in /DCIM) show up here.",
			                                        "As fotos tiradas com a câmera do Hub e com a Câmera Nintendo DSi (na pasta /DCIM) aparecem aqui."),
			                        224, p.textDim);
		} else if (preview.ok()) {
			gfx::fillRect(top, 0, 28, 256, 164, 0x8000);
			image::drawFitted(top, preview, 0, 28, 256, 164);
			std::string name = photos[sel].substr(photos[sel].rfind('/') + 1);
			char pos[24];
			snprintf(pos, sizeof(pos), "%d/%d", sel + 1, n);
			gfx::fillRect(top, 0, 172, 256, 20, 0x8000, 140);
			fonts::body.draw(top, 8, 175, name, 0xFFFF);
			fonts::body.drawRight(top, 248, 175, pos, HEXCOLOR(0xDDDDDD));
		}
		ui::drawToast(top);

		// Bottom: grid
		ui::background(bottom);
		std::vector<std::pair<ui::Rect, int>> cells;
		for (int r = 0; r < kRows + 1; r++) {
			for (int c = 0; c < kCols; c++) {
				int i = (scrollRow + r) * kCols + c;
				if (i >= n)
					break;
				ui::Rect cell{kGridX + c * kCellW + 3, kGridY + r * (kCellH + 6), kThumbW, kThumbH + 4};
				if (cell.y > 168)
					break;
				cells.push_back({cell, i});
				gfx::fillRoundRect(bottom, cell.x, cell.y, cell.w, cell.h, 6, p.panel);
				if (thumbs[i].ok()) {
					bottom.setClip(cell.x, cell.y, cell.w, cell.h > 168 - cell.y ? 168 - cell.y : cell.h);
					gfx::blit(bottom, thumbs[i].px.data(), thumbs[i].w, thumbs[i].h, cell.x + (cell.w - thumbs[i].w) / 2, cell.y + (cell.h - thumbs[i].h) / 2);
					bottom.resetClip();
				} else if (!thumbTried[i]) {
					ui::spinner(bottom, cell.x + cell.w / 2, cell.y + cell.h / 2, 6);
				}
				if (i == sel)
					gfx::strokeRoundRect(bottom, cell.x - 3, cell.y - 3, cell.w + 6, cell.h + 6, 8, 3, p.accent);
			}
		}
		if (n == 0)
			fonts::body.drawCentered(bottom, 128, 76, TR("Take a photo with the Camera app!", "Tire uma foto com o app Câmera!"), p.textDim);
		ui::footer(bottom, {{"B", TR("Back", "Voltar"), KEY_B}, {"X", TR("Delete", "Apagar"), KEY_X}, {"A", TR("View", "Ver"), KEY_A}});

		// Input
		uint32_t kd = platform::keysDown() | ui::footerTapped();
		uint32_t kr = platform::keysRepeat();
		int old = sel;
		if (n > 0) {
			if (kr & KEY_RIGHT)
				sel = sel + 1 < n ? sel + 1 : sel;
			if (kr & KEY_LEFT)
				sel = sel > 0 ? sel - 1 : 0;
			if (kr & KEY_DOWN)
				sel = sel + kCols < n ? sel + kCols : n - 1;
			if (kr & KEY_UP)
				sel = sel - kCols >= 0 ? sel - kCols : sel;
		}
		bool open = (kd & KEY_A) && n > 0;
		for (auto &c : cells) {
			if (ui::tapped(c.first)) {
				if (sel == c.second)
					open = true;
				sel = c.second;
			}
		}
		if (sel != old)
			platform::playSfx(platform::SFX_MOVE);
		if (sel / kCols < scrollRow)
			scrollRow = sel / kCols;
		if (sel / kCols >= scrollRow + kRows)
			scrollRow = sel / kCols - kRows + 1;

		bool del = (kd & KEY_X) && n > 0;
		if (open) {
			platform::playSfx(platform::SFX_SELECT);
			ViewerCtx ctx{&photos, sel, false, 0};
			while (true) {
				ui::busy(TR("Album", "Álbum"), TR("Opening photo…", "Abrindo foto…"), -1, false);
				Image full;
				if (!image::load(photos[ctx.index], full, 1024)) {
					ui::message(TR("Album", "Álbum"), TR("This photo couldn't be opened.", "Não foi possível abrir esta foto."));
					break;
				}
				char info[32];
				snprintf(info, sizeof(info), "%d/%d", ctx.index + 1, n);
				ctx.move = 0;
				image::view(full, photos[ctx.index].substr(photos[ctx.index].rfind('/') + 1), info, viewerKey, &ctx, "X", TR("Delete", "Apagar"), KEY_X);
				if (ctx.move != 0 && !ctx.deleted) {
					ctx.index = (ctx.index + ctx.move + n) % n;
					continue;
				}
				break;
			}
			sel = ctx.index;
			if (ctx.deleted)
				del = false;
			if (ctx.deleted) {
				photos.erase(photos.begin() + sel);
				thumbs.erase(thumbs.begin() + sel);
				thumbTried.erase(thumbTried.begin() + sel);
				if (sel >= (int)photos.size())
					sel = (int)photos.size() - 1;
				if (sel < 0)
					sel = 0;
				previewIndex = -1;
				ui::toast(TR("Photo deleted", "Foto apagada"));
			}
		}
		if (del) {
			std::string name = photos[sel].substr(photos[sel].rfind('/') + 1);
			if (ui::confirm(TR("Delete photo?", "Apagar foto?"), name, TR("Delete", "Apagar"), TR("Cancel", "Cancelar"))) {
				remove(photos[sel].c_str());
				photos.erase(photos.begin() + sel);
				thumbs.erase(thumbs.begin() + sel);
				thumbTried.erase(thumbTried.begin() + sel);
				if (sel >= (int)photos.size())
					sel = (int)photos.size() - 1;
				if (sel < 0)
					sel = 0;
				previewIndex = -1;
				ui::toast(TR("Photo deleted", "Foto apagada"));
			}
		}
		if (kd & KEY_B) {
			platform::playSfx(platform::SFX_BACK);
			return;
		}
		platform::present();
	}
}

} // namespace apps

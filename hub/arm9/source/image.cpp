#include "image.h"

#include <stdio.h>
#include <string.h>

#include "i18n.h"
#include "platform.h"
#include "ui.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

namespace image {

static void fromRgb(const uint8_t *rgb, int w, int h, int comp, Image &out, int maxDim) {
	Image full;
	full.w = w;
	full.h = h;
	full.px.resize((size_t)w * h);
	for (int i = 0; i < w * h; i++) {
		const uint8_t *p = rgb + i * comp;
		uint8_t r = p[0], g = p[1], b = p[2];
		if (comp == 4 && p[3] < 255) {
			// Blend transparent pixels over white
			int a = p[3];
			r = (r * a + 255 * (255 - a)) / 255;
			g = (g * a + 255 * (255 - a)) / 255;
			b = (b * a + 255 * (255 - a)) / 255;
		}
		full.px[i] = RGB888(r, g, b);
	}
	if (w > maxDim || h > maxDim) {
		fit(full, out, maxDim, maxDim);
	} else {
		out = std::move(full);
	}
}

bool decode(const uint8_t *data, size_t size, Image &out, int maxDim) {
	int w, h, comp;
	uint8_t *rgb = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 0);
	if (!rgb)
		return false;
	if (comp < 3) {
		stbi_image_free(rgb);
		rgb = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 3);
		comp = 3;
		if (!rgb)
			return false;
	}
	fromRgb(rgb, w, h, comp, out, maxDim);
	stbi_image_free(rgb);
	return out.ok();
}

bool load(const std::string &path, Image &out, int maxDim) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0 || size > 8 * 1024 * 1024) {
		fclose(f);
		return false;
	}
	std::vector<uint8_t> buf(size);
	size_t n = fread(buf.data(), 1, size, f);
	fclose(f);
	return n == (size_t)size && decode(buf.data(), buf.size(), out, maxDim);
}

void resize(const Image &src, Image &dst, int w, int h) {
	dst.w = w;
	dst.h = h;
	dst.px.assign((size_t)w * h, 0x8000);
	if (!src.ok() || w <= 0 || h <= 0)
		return;
	for (int y = 0; y < h; y++) {
		int sy0 = y * src.h / h, sy1 = (y + 1) * src.h / h;
		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		for (int x = 0; x < w; x++) {
			int sx0 = x * src.w / w, sx1 = (x + 1) * src.w / w;
			if (sx1 <= sx0)
				sx1 = sx0 + 1;
			int r = 0, g = 0, b = 0, n = 0;
			for (int yy = sy0; yy < sy1; yy++) {
				const uint16_t *row = &src.px[(size_t)yy * src.w];
				for (int xx = sx0; xx < sx1; xx++) {
					uint16_t c = row[xx];
					r += c & 31;
					g += (c >> 5) & 31;
					b += (c >> 10) & 31;
					n++;
				}
			}
			dst.px[(size_t)y * w + x] = (uint16_t)((r / n) | ((g / n) << 5) | ((b / n) << 10) | 0x8000);
		}
	}
}

void fit(const Image &src, Image &dst, int w, int h) {
	if (!src.ok())
		return;
	int dw = w, dh = src.h * w / src.w;
	if (dh > h) {
		dh = h;
		dw = src.w * h / src.h;
	}
	if (dw < 1)
		dw = 1;
	if (dh < 1)
		dh = 1;
	if (dw >= src.w && dh >= src.h) {
		dst = src; // never upscale here
		return;
	}
	resize(src, dst, dw, dh);
}

void drawFitted(Surface &s, const Image &fitted, int x, int y, int w, int h) {
	if (!fitted.ok())
		return;
	if (fitted.w <= w && fitted.h <= h)
		gfx::blit(s, fitted.px.data(), fitted.w, fitted.h, x + (w - fitted.w) / 2, y + (h - fitted.h) / 2);
	else
		gfx::blitScaled(s, fitted.px.data(), fitted.w, fitted.h, x, y, w, h);
}

void view(const Image &img, const std::string &title, const std::string &info, ViewerKeyFn onKey, void *user,
          const std::string &extraButton, const std::string &extraLabel, uint32_t extraKey) {
	if (!img.ok())
		return;
	Image fitted;
	fit(img, fitted, 256, 192);
	// Upscale small images on the top screen (integer factor)
	int scale = 1;
	while (fitted.w * (scale + 1) <= 256 && fitted.h * (scale + 1) <= 192 && scale < 4)
		scale++;
	int panX = img.w / 2 - 128, panY = img.h / 2 - 84;
	int dragX = 0, dragY = 0, dragPanX = 0, dragPanY = 0;
	bool dragging = false;
	bool showInfo = true;

	while (true) {
		platform::scanInput();
		Surface &top = platform::top();
		Surface &bottom = platform::bottom();
		const ui::Palette &p = ui::pal();

		gfx::clear(top, 0x8000);
		int fw = fitted.w * scale, fh = fitted.h * scale;
		int fx = (256 - fw) / 2, fy = (192 - fh) / 2;
		gfx::blitScaled(top, fitted.px.data(), fitted.w, fitted.h, fx, fy, fw, fh);
		// Rectangle of the detail view
		bool detail = img.w > 256 || img.h > 168;
		if (detail) {
			int rx = fx + panX * fw / img.w, ry = fy + panY * fh / img.h;
			int rw = 256 * fw / img.w, rh = 168 * fh / img.h;
			gfx::rect(top, rx, ry, rw > fw ? fw : rw, rh > fh ? fh : rh, p.accent);
		}
		if (showInfo) {
			gfx::fillRect(top, 0, 0, 256, 22, 0x8000, 150);
			fonts::bold.drawEllipsized(top, 8, 4, title, 150, 0xFFFF);
			fonts::body.drawRight(top, 248, 4, info, HEXCOLOR(0xDDDDDD));
		}

		// Bottom: 1:1 detail (or the image centred if small)
		gfx::clear(bottom, 0x8000);
		if (panX > img.w - 256)
			panX = img.w - 256;
		if (panY > img.h - 168)
			panY = img.h - 168;
		if (panX < 0)
			panX = 0;
		if (panY < 0)
			panY = 0;
		int vw = img.w < 256 ? img.w : 256, vh = img.h < 168 ? img.h : 168;
		int ox = (256 - vw) / 2, oy = (168 - vh) / 2;
		for (int y = 0; y < vh; y++)
			memcpy(&bottom.px[(oy + y) * 256 + ox], &img.px[(size_t)(panY + y) * img.w + panX], vw * 2);
		std::vector<ui::Hint> hints = {{"B", TR("Back", "Voltar"), KEY_B}};
		if (!extraButton.empty())
			hints.push_back({extraButton, extraLabel, extraKey});
		hints.push_back({"Y", TR("Info", "Info"), KEY_Y});
		ui::footer(bottom, hints);

		// Input
		uint32_t kd = platform::keysDown() | ui::footerTapped();
		uint32_t kh = platform::keysHeld();
		int speed = 4;
		if (kh & KEY_LEFT)
			panX -= speed;
		if (kh & KEY_RIGHT)
			panX += speed;
		if (kh & KEY_UP)
			panY -= speed;
		if (kh & KEY_DOWN)
			panY += speed;
		if (platform::touchDown() && platform::touchY() < 168) {
			dragging = true;
			dragX = platform::touchX();
			dragY = platform::touchY();
			dragPanX = panX;
			dragPanY = panY;
		}
		if (dragging && platform::touching()) {
			panX = dragPanX - (platform::touchX() - dragX);
			panY = dragPanY - (platform::touchY() - dragY);
		}
		if (!platform::touching())
			dragging = false;
		if (kd & KEY_Y)
			showInfo = !showInfo;
		if (kd & KEY_B) {
			platform::playSfx(platform::SFX_BACK);
			return;
		}
		if (onKey && (kd & ~(KEY_B | KEY_Y | KEY_TOUCH)) && onKey(user, kd))
			return;
		platform::present();
	}
}

} // namespace image

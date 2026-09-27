// Camera: live preview from the inner/outer DSi camera, colour effects,
// self-timer, and 640x480 photos saved as JPEG in /DCIM/100TWLHB.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "apps.h"
#include "config.h"
#include "i18n.h"
#include "image.h"
#include "platform.h"
#include "ui.h"

#ifdef __NDS__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x)
#endif
#include "stb_image_write.h"

namespace apps {

namespace {

enum Effect { FX_NORMAL, FX_MONO, FX_SEPIA, FX_NEGATIVE, FX_WARM, FX_COOL, FX_COUNT };

const char *effectName(int e) {
	switch (e) {
		case FX_MONO:
			return TR("B&W", "P&B");
		case FX_SEPIA:
			return TR("Sepia", "Sépia");
		case FX_NEGATIVE:
			return TR("Negative", "Negativo");
		case FX_WARM:
			return TR("Warm", "Quente");
		case FX_COOL:
			return TR("Cool", "Frio");
		default:
			return "Normal";
	}
}

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// Applies an effect to one 8-bit RGB pixel
inline void applyEffect(int fx, int &r, int &g, int &b) {
	switch (fx) {
		case FX_MONO: {
			int y = (r * 77 + g * 150 + b * 29) >> 8;
			r = g = b = y;
			break;
		}
		case FX_SEPIA: {
			int y = (r * 77 + g * 150 + b * 29) >> 8;
			r = clamp255(y * 107 / 100 + 20);
			g = clamp255(y * 90 / 100 + 8);
			b = clamp255(y * 68 / 100);
			break;
		}
		case FX_NEGATIVE:
			r = 255 - r;
			g = 255 - g;
			b = 255 - b;
			break;
		case FX_WARM:
			r = clamp255(r + 22);
			g = clamp255(g + 6);
			b = clamp255(b - 18);
			break;
		case FX_COOL:
			r = clamp255(r - 18);
			g = clamp255(g + 4);
			b = clamp255(b + 24);
			break;
	}
}

// 15-bit lookup table used for the live preview
uint16_t *effectLut = nullptr;
int lutEffect = -1;

void buildLut(int fx) {
	if (!effectLut)
		effectLut = (uint16_t *)malloc(32768 * sizeof(uint16_t));
	for (int c = 0; c < 32768; c++) {
		int r = (c & 31) << 3, g = ((c >> 5) & 31) << 3, b = ((c >> 10) & 31) << 3;
		applyEffect(fx, r, g, b);
		effectLut[c] = (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000);
	}
	lutEffect = fx;
}

std::string photoDir() { return platform::root() + "/DCIM/100TWLHB"; }

std::string nextPhotoPath() {
	mkdir((platform::root() + "/DCIM").c_str(), 0777);
	mkdir(photoDir().c_str(), 0777);
	int highest = 0;
	DIR *d = opendir(photoDir().c_str());
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) != nullptr) {
			if (strncasecmp(e->d_name, "TWLH", 4) == 0) {
				int v = atoi(e->d_name + 4);
				if (v > highest)
					highest = v;
			}
		}
		closedir(d);
	}
	char name[64];
	snprintf(name, sizeof(name), "/TWLH%04d.JPG", highest + 1);
	return photoDir() + name;
}

// YUV 4:2:2 (Y1 Cb Y2 Cr) to RGB888 with an effect
void yuvToRgb(const uint16_t *yuv, uint8_t *rgb, int width, int height, int fx) {
	for (int py = 0; py < height; py++) {
		for (int px = 0; px < width; px += 2) {
			const uint8_t *v = (const uint8_t *)(yuv + py * width + px);
			int y1 = v[0], cb = v[1] - 128, y2 = v[2], cr = v[3] - 128;
			int rd = cr + (cr >> 2) + (cr >> 3) + (cr >> 5);
			int gd = ((cb >> 2) + (cb >> 4) + (cb >> 5)) + ((cr >> 1) + (cr >> 3) + (cr >> 4) + (cr >> 5));
			int bd = cb + (cb >> 1) + (cb >> 2) + (cb >> 6);
			uint8_t *dst = rgb + (py * width + px) * 3;
			for (int k = 0; k < 2; k++) {
				int y = k ? y2 : y1;
				int r = clamp255(y + rd), g = clamp255(y - gd), b = clamp255(y + bd);
				applyEffect(fx, r, g, b);
				dst[k * 3] = r;
				dst[k * 3 + 1] = g;
				dst[k * 3 + 2] = b;
			}
		}
	}
}

struct WriteCtx {
	FILE *f;
	bool ok;
};

void writeFn(void *ctx, void *data, int size) {
	WriteCtx *w = (WriteCtx *)ctx;
	if (fwrite(data, 1, size, w->f) != (size_t)size)
		w->ok = false;
}

} // namespace

void camera() {
	const ui::Palette &p = ui::pal();
	int fx = config::ini().getInt("CAMERA", "EFFECT", FX_NORMAL);
	if (fx < 0 || fx >= FX_COUNT)
		fx = FX_NORMAL;
	bool inner = config::ini().getInt("CAMERA", "INNER", 0) != 0;
	int timer = 0;      // frames left on the self-timer
	int flash = 0;
	Image lastThumb;
	std::string lastPath;

#ifdef __NDS__
	const int kNdma = 1;
	static uint16_t previewBuf[256 * 192] __attribute__((aligned(32)));
	ui::busy(TR("Camera", "Câmera"), TR("Starting the camera…", "Iniciando a câmera…"), -1, false);
	if (!cameraInit()) {
		platform::playSfx(platform::SFX_ERROR);
		ui::message(TR("Camera", "Câmera"), TR("The camera couldn't be started. Make sure TWiLight Menu++ runs in DSi mode with full access (e.g. through Unlaunch).",
		                                       "Não foi possível iniciar a câmera. Verifique se o TWiLight Menu++ roda no modo DSi com acesso total (por exemplo, pelo Unlaunch)."));
		cameraDeinit();
		return;
	}
	if (!cameraSelect(inner ? CAMERA_INNER : CAMERA_OUTER)) {
		ui::message(TR("Camera", "Câmera"), TR("Couldn't select the camera.", "Não foi possível selecionar a câmera."));
		cameraDeinit();
		return;
	}
	platform::setTopPassthrough(true);
	uint16_t *vram = platform::topVram();
	bool transferStarted = false, toBuffer = false;
#endif

	while (true) {
		platform::scanInput();

#ifdef __NDS__
		// Keep a preview transfer running. With an effect, frames go through
		// RAM so they can be processed.
		if (!ndmaBusy(kNdma) || !cameraTransferActive()) {
			if (transferStarted && toBuffer && lutEffect >= 0) {
				// A processed frame is ready
				DC_InvalidateRange(previewBuf, sizeof(previewBuf));
				for (int i = 0; i < 256 * 192; i++)
					vram[i] = effectLut[previewBuf[i] & 0x7FFF];
			}
			if (fx != FX_NORMAL && lutEffect != fx)
				buildLut(fx);
			toBuffer = fx != FX_NORMAL;
			cameraStartTransfer(toBuffer ? previewBuf : vram, MCUREG_APT_SEQ_CMD_PREVIEW, kNdma);
			transferStarted = true;
		}
#else
		gfx::gradientV(platform::top(), 0, 0, 256, 192, HEXCOLOR(0x6090C0), HEXCOLOR(0x305030));
#endif

		// Bottom screen controls (always dark, like a camera app)
		Surface &b = platform::bottom();
		gfx::clear(b, HEXCOLOR(0x1E1E1E));
		// Effect selector: "<  Sepia  >"
		ui::Rect fxPrev{40, 6, 40, 26}, fxNext{176, 6, 40, 26};
		gfx::fillRoundRect(b, 40, 6, 176, 26, 13, HEXCOLOR(0x3A3A3A));
		fonts::bold.drawCentered(b, 128, 12, effectName(fx), fx == FX_NORMAL ? 0xFFFF : p.accent);
		fonts::title.drawCentered(b, 56, 10, "<", HEXCOLOR(0xBBBBBB));
		fonts::title.drawCentered(b, 200, 10, ">", HEXCOLOR(0xBBBBBB));
		ui::Rect shutter{98, 58, 60, 60}, sw{30, 68, 40, 40}, alb{186, 64, 48, 48}, tim{112, 130, 32, 22};
		gfx::fillCircle(b, 128, 88, 30, 0xFFFF);
		gfx::strokeCircle(b, 128, 88, 26, 3, HEXCOLOR(0x1E1E1E));
		if (platform::touching() && shutter.contains(platform::touchX(), platform::touchY()))
			gfx::fillCircle(b, 128, 88, 22, HEXCOLOR(0xCCCCCC));
		gfx::fillCircle(b, 50, 88, 20, HEXCOLOR(0x3A3A3A));
		ui::icon(b, ICON_SWITCHCAM, ICONSIZE_24, 38, 76, 0xFFFF);
		gfx::fillRoundRect(b, alb.x, alb.y, alb.w, alb.h, 10, HEXCOLOR(0x3A3A3A));
		if (lastThumb.ok()) {
			b.setClip(alb.x + 2, alb.y + 2, alb.w - 4, alb.h - 4);
			gfx::blit(b, lastThumb.px.data(), lastThumb.w, lastThumb.h, alb.x + (alb.w - lastThumb.w) / 2, alb.y + (alb.h - lastThumb.h) / 2);
			b.resetClip();
		} else {
			ui::icon(b, ICON_ALBUM, ICONSIZE_24, alb.x + 12, alb.y + 12, HEXCOLOR(0xBBBBBB));
		}
		std::string timerText = timer > 0 ? std::to_string(timer / 60 + 1) : std::string("3s");
		gfx::fillRoundRect(b, tim.x, tim.y, tim.w, tim.h, 11, timer > 0 ? p.accent : HEXCOLOR(0x3A3A3A));
		fonts::bold.drawCentered(b, 128, tim.y + 4, timerText, timer > 0 ? HEXCOLOR(0x101010) : 0xFFFF);
		fonts::body.drawCentered(b, 50, 114, inner ? TR("Inner", "Interna") : TR("Outer", "Externa"), HEXCOLOR(0xBBBBBB));
		if (!lastPath.empty())
			fonts::body.drawCentered(b, 128, 154, lastPath.substr(lastPath.rfind('/') + 1), HEXCOLOR(0x888888));
		ui::applyTheme(true); // footer in dark colours
		ui::footer(b, {{"B", TR("Back", "Voltar"), KEY_B}, {"Y", TR("Switch", "Trocar"), KEY_Y}, {"X", TR("Timer", "Timer"), KEY_X}, {"L", TR("Photo", "Foto"), KEY_L}});
		ui::applyTheme(config::darkTheme());

		uint32_t kd = platform::keysDown() | ui::footerTapped();
		bool take = (kd & (KEY_L | KEY_R | KEY_A)) || ui::tapped(shutter);
		if (ui::tapped(fxNext))
			kd |= KEY_RIGHT;
		if (ui::tapped(fxPrev))
			kd |= KEY_LEFT;
		if (kd & KEY_RIGHT) {
			fx = (fx + 1) % FX_COUNT;
			platform::playSfx(platform::SFX_MOVE);
		}
		if (kd & KEY_LEFT) {
			fx = (fx + FX_COUNT - 1) % FX_COUNT;
			platform::playSfx(platform::SFX_MOVE);
		}
		if ((kd & KEY_X) || ui::tapped(tim)) {
			timer = timer > 0 ? 0 : 180;
			platform::playSfx(platform::SFX_TOGGLE);
		}
		if ((kd & KEY_Y) || ui::tapped(sw)) {
			inner = !inner;
			platform::playSfx(platform::SFX_TOGGLE);
#ifdef __NDS__
			while (ndmaBusy(kNdma))
				platform::waitVBlank();
			cameraStopTransfer();
			cameraSelect(inner ? CAMERA_INNER : CAMERA_OUTER);
			transferStarted = false;
#endif
		}
		if (ui::tapped(alb) && !lastPath.empty()) {
#ifdef __NDS__
			while (ndmaBusy(kNdma))
				platform::waitVBlank();
			cameraStopTransfer();
			platform::setTopPassthrough(false);
#endif
			album();
#ifdef __NDS__
			platform::setTopPassthrough(true);
			transferStarted = false;
#endif
		}
		if (timer > 0) {
			timer--;
			if (timer % 60 == 0 && timer > 0)
				platform::playSfx(platform::SFX_MOVE);
			if (timer == 0)
				take = true;
			else
				take = false;
		}

		if (take) {
			platform::playSfx(platform::SFX_SHUTTER);
			flash = 6;
			std::string path = nextPhotoPath();
#ifdef __NDS__
			while (ndmaBusy(kNdma))
				platform::waitVBlank();
			cameraStopTransfer();
			uint16_t *yuv = (uint16_t *)malloc(640 * 480 * 2);
			uint8_t *rgb = (uint8_t *)malloc(640 * 480 * 3);
			bool saved = false;
			if (yuv && rgb) {
				platform::setBrightness(1, 12);
				DC_FlushRange(yuv, 640 * 480 * 2);
				cameraStartTransfer(yuv, MCUREG_APT_SEQ_CMD_CAPTURE, kNdma);
				while (ndmaBusy(kNdma))
					platform::waitVBlank();
				cameraStopTransfer();
				DC_InvalidateRange(yuv, 640 * 480 * 2);
				platform::setBrightness(1, 0);
				ui::busy(TR("Saving photo", "Salvando foto"), path.substr(path.rfind('/') + 1), -1, false);
				yuvToRgb(yuv, rgb, 640, 480, fx);
				free(yuv);
				yuv = nullptr;
				FILE *f = fopen(path.c_str(), "wb");
				if (f) {
					WriteCtx ctx{f, true};
					saved = stbi_write_jpg_to_func(writeFn, &ctx, 640, 480, 3, rgb, 92) && ctx.ok;
					fclose(f);
					if (!saved)
						remove(path.c_str());
				}
				if (saved) {
					// Thumbnail for the album button
					Image full;
					full.w = 640;
					full.h = 480;
					full.px.resize(640 * 480);
					for (int i = 0; i < 640 * 480; i++)
						full.px[i] = RGB888(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
					image::resize(full, lastThumb, 60, 45);
				}
			}
			if (yuv)
				free(yuv);
			if (rgb)
				free(rgb);
			// The next cameraStartTransfer() switches back to preview mode
			transferStarted = false;
#else
			bool saved = true;
			Image fake;
			fake.w = 60;
			fake.h = 45;
			fake.px.assign(60 * 45, HEXCOLOR(0x6090C0));
			lastThumb = fake;
#endif
			if (saved) {
				lastPath = path;
				ui::toast(TR("Photo saved", "Foto salva"), 90);
			} else {
				platform::playSfx(platform::SFX_ERROR);
				ui::message(TR("Camera", "Câmera"), TR("Couldn't save the photo. Is the SD card full or locked?", "Não foi possível salvar a foto. O cartão SD está cheio ou travado?"));
			}
		}

		if (kd & KEY_B) {
			platform::playSfx(platform::SFX_BACK);
			break;
		}
		if (flash > 0) {
			flash--;
			platform::setBrightness(1, flash * 2);
		}
		platform::present(false, true);
	}

#ifdef __NDS__
	while (ndmaBusy(kNdma))
		platform::waitVBlank();
	cameraStopTransfer();
	cameraDeinit();
	platform::setTopPassthrough(false);
#endif
	platform::setBrightness(1, 0);
	config::ini().setInt("CAMERA", "EFFECT", fx);
	config::ini().setInt("CAMERA", "INNER", inner);
	config::save();
}

} // namespace apps

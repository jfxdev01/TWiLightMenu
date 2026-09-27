// DSi Dash — decodificacao de imagens (JPEG/PNG/GIF/BMP via stb_image) para RGB555 reduzido
#include "common.h"
#include "image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 4096
#include "stb_image.h"

// reduz RGB888 (sw x sh) para caber em maxW x maxH, com media por caixa
static u16* downscale(const u8* rgb, int sw, int sh, int maxW, int maxH, int* ow, int* oh) {
	int w = sw, h = sh;
	if (w > maxW) {
		h = h * maxW / w;
		w = maxW;
	}
	if (h > maxH) {
		w = w * maxH / h;
		h = maxH;
	}
	w = MAX(1, w);
	h = MAX(1, h);
	u16* out = (u16*)malloc(w * h * 2);
	if (!out) return NULL;
	for (int y = 0; y < h; y++) {
		int y0 = y * sh / h, y1 = MAX(y0 + 1, (y + 1) * sh / h);
		for (int x = 0; x < w; x++) {
			int x0 = x * sw / w, x1 = MAX(x0 + 1, (x + 1) * sw / w);
			// amostra no maximo 4x4 pontos por pixel de saida
			int sx = MAX(1, (x1 - x0) / 4), sy = MAX(1, (y1 - y0) / 4);
			int r = 0, g = 0, b = 0, n = 0;
			for (int yy = y0; yy < y1; yy += sy)
				for (int xx = x0; xx < x1; xx += sx) {
					const u8* p = rgb + (yy * sw + xx) * 3;
					r += p[0];
					g += p[1];
					b += p[2];
					n++;
				}
			out[y * w + x] = COL8(r / n, g / n, b / n);
		}
	}
	*ow = w;
	*oh = h;
	return out;
}

u16* imgDecodeMem(const u8* data, int len, int maxW, int maxH, int* ow, int* oh) {
	int w, h, n;
	u8* rgb = stbi_load_from_memory(data, len, &w, &h, &n, 3);
	if (!rgb) return NULL;
	u16* out = downscale(rgb, w, h, maxW, maxH, ow, oh);
	stbi_image_free(rgb);
	return out;
}

u16* imgLoadFile(const char* path, int maxW, int maxH, int* ow, int* oh) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0 || sz > 8 * 1024 * 1024) {
		fclose(f);
		return NULL;
	}
	u8* buf = (u8*)malloc(sz);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	size_t rd = fread(buf, 1, sz, f);
	fclose(f);
	u16* out = rd == (size_t)sz ? imgDecodeMem(buf, sz, maxW, maxH, ow, oh) : NULL;
	free(buf);
	return out;
}

bool imgIsImageName(const char* name) {
	const char* e = strrchr(name, '.');
	if (!e) return false;
	return !strcasecmp(e, ".jpg") || !strcasecmp(e, ".jpeg") || !strcasecmp(e, ".png") || !strcasecmp(e, ".bmp") || !strcasecmp(e, ".gif");
}

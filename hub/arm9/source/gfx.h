// Software renderer used by TWiLight Hub.
//
// Everything is drawn into 16-bit (BGR555 + bit 15) surfaces in main RAM that
// are copied to VRAM once per frame. This file has no dependency on libnds so
// that the UI can also be rendered on a PC (see tools/sim).
#pragma once

#include <stdint.h>

#define RGB15C(r, g, b) ((uint16_t)(((r) & 31) | (((g) & 31) << 5) | (((b) & 31) << 10) | 0x8000))
// 8-bit per channel helper
#define RGB888(r, g, b) RGB15C((r) >> 3, (g) >> 3, (b) >> 3)
#define HEXCOLOR(c) RGB888(((c) >> 16) & 0xFF, ((c) >> 8) & 0xFF, (c) & 0xFF)

struct Surface {
	uint16_t *px;
	int w, h;
	// Clip rectangle (inclusive-exclusive)
	int cx0, cy0, cx1, cy1;

	void init(uint16_t *buffer, int width, int height);
	void setClip(int x, int y, int w, int h);
	void resetClip();
};

namespace gfx {

uint16_t blend(uint16_t dst, uint16_t src, int alpha); // alpha 0..255
uint16_t mix(uint16_t a, uint16_t b, int t);            // t 0..255 -> a..b
uint16_t shade(uint16_t c, int amount);                 // amount -255..255 (darker/lighter)

void clear(Surface &s, uint16_t color);
void pixel(Surface &s, int x, int y, uint16_t color, int alpha = 255);
void fillRect(Surface &s, int x, int y, int w, int h, uint16_t color, int alpha = 255);
void hline(Surface &s, int x, int y, int w, uint16_t color, int alpha = 255);
void vline(Surface &s, int x, int y, int h, uint16_t color, int alpha = 255);
void rect(Surface &s, int x, int y, int w, int h, uint16_t color, int alpha = 255);
void gradientV(Surface &s, int x, int y, int w, int h, uint16_t top, uint16_t bottom);

// Anti-aliased primitives
void fillRoundRect(Surface &s, int x, int y, int w, int h, int r, uint16_t color, int alpha = 255);
void strokeRoundRect(Surface &s, int x, int y, int w, int h, int r, int thickness, uint16_t color, int alpha = 255);
void gradientRoundRect(Surface &s, int x, int y, int w, int h, int r, uint16_t top, uint16_t bottom);
void fillCircle(Surface &s, int cx, int cy, int r, uint16_t color, int alpha = 255);
void strokeCircle(Surface &s, int cx, int cy, int r, int thickness, uint16_t color, int alpha = 255);
void line(Surface &s, int x0, int y0, int x1, int y1, uint16_t color);
// Soft drop shadow for a rounded rectangle
void shadow(Surface &s, int x, int y, int w, int h, int r, int spread, int alpha);

// 8-bit alpha mask tinted with a color
void mask(Surface &s, const uint8_t *m, int mw, int mh, int x, int y, uint16_t color, int alpha = 255);
// 16-bit image (bit 15 = opaque) with optional nearest-neighbour scaling
void blit(Surface &s, const uint16_t *img, int iw, int ih, int x, int y);
void blitScaled(Surface &s, const uint16_t *img, int iw, int ih, int x, int y, int w, int h);

} // namespace gfx

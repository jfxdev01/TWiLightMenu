#include "gfx.h"

#include <stdlib.h>
#include <string.h>

void Surface::init(uint16_t *buffer, int width, int height) {
	px = buffer;
	w = width;
	h = height;
	resetClip();
}

void Surface::setClip(int x, int y, int cw, int ch) {
	cx0 = x < 0 ? 0 : x;
	cy0 = y < 0 ? 0 : y;
	cx1 = x + cw > w ? w : x + cw;
	cy1 = y + ch > h ? h : y + ch;
}

void Surface::resetClip() {
	cx0 = 0;
	cy0 = 0;
	cx1 = w;
	cy1 = h;
}

namespace gfx {

uint16_t blend(uint16_t dst, uint16_t src, int alpha) {
	if (alpha >= 255)
		return src;
	if (alpha <= 0)
		return dst;
	int a = alpha + (alpha >> 7);
	int ia = 256 - a;
	int r = ((src & 31) * a + (dst & 31) * ia) >> 8;
	int g = (((src >> 5) & 31) * a + ((dst >> 5) & 31) * ia) >> 8;
	int b = (((src >> 10) & 31) * a + ((dst >> 10) & 31) * ia) >> 8;
	return (uint16_t)(r | (g << 5) | (b << 10) | 0x8000);
}

uint16_t mix(uint16_t a, uint16_t b, int t) {
	return blend(a, b, t);
}

uint16_t shade(uint16_t c, int amount) {
	if (amount < 0)
		return blend(c, 0x8000, -amount);
	return blend(c, 0xFFFF, amount);
}

void clear(Surface &s, uint16_t color) {
	uint32_t v = color | ((uint32_t)color << 16);
	uint32_t *p = (uint32_t *)s.px;
	int n = (s.w * s.h) / 2;
	for (int i = 0; i < n; i++)
		p[i] = v;
}

void pixel(Surface &s, int x, int y, uint16_t color, int alpha) {
	if (x < s.cx0 || y < s.cy0 || x >= s.cx1 || y >= s.cy1 || alpha <= 0)
		return;
	uint16_t *p = &s.px[y * s.w + x];
	*p = blend(*p, color, alpha);
}

void fillRect(Surface &s, int x, int y, int w, int h, uint16_t color, int alpha) {
	int x0 = x < s.cx0 ? s.cx0 : x;
	int y0 = y < s.cy0 ? s.cy0 : y;
	int x1 = x + w > s.cx1 ? s.cx1 : x + w;
	int y1 = y + h > s.cy1 ? s.cy1 : y + h;
	if (x0 >= x1 || y0 >= y1 || alpha <= 0)
		return;
	for (int yy = y0; yy < y1; yy++) {
		uint16_t *p = &s.px[yy * s.w + x0];
		if (alpha >= 255) {
			for (int xx = x0; xx < x1; xx++)
				*p++ = color;
		} else {
			for (int xx = x0; xx < x1; xx++, p++)
				*p = blend(*p, color, alpha);
		}
	}
}

void hline(Surface &s, int x, int y, int w, uint16_t color, int alpha) {
	fillRect(s, x, y, w, 1, color, alpha);
}

void vline(Surface &s, int x, int y, int h, uint16_t color, int alpha) {
	fillRect(s, x, y, 1, h, color, alpha);
}

void rect(Surface &s, int x, int y, int w, int h, uint16_t color, int alpha) {
	hline(s, x, y, w, color, alpha);
	hline(s, x, y + h - 1, w, color, alpha);
	vline(s, x, y + 1, h - 2, color, alpha);
	vline(s, x + w - 1, y + 1, h - 2, color, alpha);
}

// 8-bit per channel interpolation with 2x2 ordered dithering, to avoid the
// banding of 15-bit gradients
static const uint8_t kBayer[2][2] = {{0, 4}, {6, 2}};

static inline void lerp8(uint16_t a, uint16_t b, int t, int &r, int &g, int &bl) {
	int ar = (a & 31) << 3, ag = ((a >> 5) & 31) << 3, ab = ((a >> 10) & 31) << 3;
	int br = (b & 31) << 3, bg = ((b >> 5) & 31) << 3, bb = ((b >> 10) & 31) << 3;
	r = ar + (br - ar) * t / 255;
	g = ag + (bg - ag) * t / 255;
	bl = ab + (bb - ab) * t / 255;
}

static inline uint16_t dither(int r, int g, int b, int x, int y) {
	int d = kBayer[y & 1][x & 1];
	r = (r + d) >> 3;
	g = (g + d) >> 3;
	b = (b + d) >> 3;
	if (r > 31)
		r = 31;
	if (g > 31)
		g = 31;
	if (b > 31)
		b = 31;
	return (uint16_t)(r | (g << 5) | (b << 10) | 0x8000);
}

void gradientV(Surface &s, int x, int y, int w, int h, uint16_t top, uint16_t bottom) {
	int x0 = x < s.cx0 ? s.cx0 : x;
	int x1 = x + w > s.cx1 ? s.cx1 : x + w;
	for (int i = 0; i < h; i++) {
		int yy = y + i;
		if (yy < s.cy0 || yy >= s.cy1)
			continue;
		int r, g, b;
		lerp8(top, bottom, h > 1 ? i * 255 / (h - 1) : 0, r, g, b);
		uint16_t c0 = dither(r, g, b, 0, yy), c1 = dither(r, g, b, 1, yy);
		uint16_t *p = &s.px[yy * s.w];
		for (int xx = x0; xx < x1; xx++)
			p[xx] = (xx & 1) ? c1 : c0;
	}
}

// ---------------------------------------------------------------------------
// Anti-aliased rounded shapes, using cached quarter-circle coverage tables
// (4x4 supersampling, computed once per radius).
// ---------------------------------------------------------------------------

static const int kMaxRadius = 64;
static uint8_t *cornerTables[kMaxRadius + 1];

// Coverage table for the top-left corner of radius r: r*r bytes
static const uint8_t *cornerTable(int r) {
	if (r > kMaxRadius)
		r = kMaxRadius;
	if (cornerTables[r])
		return cornerTables[r];
	uint8_t *t = (uint8_t *)malloc(r * r > 0 ? r * r : 1);
	const int c = r * 8, rr = c * c;
	for (int j = 0; j < r; j++) {
		for (int i = 0; i < r; i++) {
			int count = 0;
			for (int sy = 0; sy < 4; sy++) {
				int dy = c - (j * 8 + 1 + sy * 2);
				for (int sx = 0; sx < 4; sx++) {
					int dx = c - (i * 8 + 1 + sx * 2);
					if (dx * dx + dy * dy <= rr)
						count++;
				}
			}
			t[j * r + i] = (uint8_t)(count * 255 / 16);
		}
	}
	cornerTables[r] = t;
	return t;
}

// Coverage (0-255) of pixel (px, py) by a rounded rectangle
static inline int rrCoverage(int px, int py, int x, int y, int w, int h, int r, const uint8_t *table) {
	if (px < x || py < y || px >= x + w || py >= y + h)
		return 0;
	int lx = px - x, ly = py - y;
	int cx = -1, cy = -1;
	if (lx < r)
		cx = lx;
	else if (lx >= w - r)
		cx = w - 1 - lx;
	if (ly < r)
		cy = ly;
	else if (ly >= h - r)
		cy = h - 1 - ly;
	if (cx < 0 || cy < 0)
		return 255;
	return table[cy * r + cx];
}

static int clampRadius(int r, int w, int h) {
	if (r * 2 > w)
		r = w / 2;
	if (r * 2 > h)
		r = h / 2;
	if (r < 0)
		r = 0;
	if (r > kMaxRadius)
		r = kMaxRadius;
	return r;
}

void fillRoundRect(Surface &s, int x, int y, int w, int h, int r, uint16_t color, int alpha) {
	if (w <= 0 || h <= 0)
		return;
	r = clampRadius(r, w, h);
	if (r == 0) {
		fillRect(s, x, y, w, h, color, alpha);
		return;
	}
	const uint8_t *table = cornerTable(r);
	// Middle band without corners
	fillRect(s, x, y + r, w, h - 2 * r, color, alpha);
	for (int band = 0; band < 2; band++) {
		int y0 = band == 0 ? y : y + h - r;
		for (int yy = y0; yy < y0 + r; yy++) {
			if (yy < s.cy0 || yy >= s.cy1)
				continue;
			// Straight part of the row
			fillRect(s, x + r, yy, w - 2 * r, 1, color, alpha);
			for (int i = 0; i < r; i++) {
				int cov = rrCoverage(x + i, yy, x, y, w, h, r, table);
				pixel(s, x + i, yy, color, cov * alpha / 255);
				cov = rrCoverage(x + w - 1 - i, yy, x, y, w, h, r, table);
				pixel(s, x + w - 1 - i, yy, color, cov * alpha / 255);
			}
		}
	}
}

void strokeRoundRect(Surface &s, int x, int y, int w, int h, int r, int t, uint16_t color, int alpha) {
	if (w <= 0 || h <= 0 || t <= 0)
		return;
	r = clampRadius(r, w, h);
	int ix = x + t, iy = y + t, iw = w - 2 * t, ih = h - 2 * t;
	int ir = r - t;
	if (ir < 0)
		ir = 0;
	ir = clampRadius(ir, iw, ih);
	const uint8_t *ot = cornerTable(r);
	const uint8_t *it = cornerTable(ir);
	int y0 = y < s.cy0 ? s.cy0 : y;
	int y1 = y + h > s.cy1 ? s.cy1 : y + h;
	for (int yy = y0; yy < y1; yy++) {
		bool inBand = (yy >= iy && yy < iy + ih);
		for (int xx = x; xx < x + w; xx++) {
			// Skip the flat interior quickly
			if (inBand && xx >= ix + ir && xx < ix + iw - ir && yy >= iy + ir && yy < iy + ih - ir) {
				xx = ix + iw - ir - 1;
				continue;
			}
			int cov = (r ? rrCoverage(xx, yy, x, y, w, h, r, ot) : ((xx >= x && xx < x + w && yy >= y && yy < y + h) ? 255 : 0));
			if (iw > 0 && ih > 0) {
				int icov = ir ? rrCoverage(xx, yy, ix, iy, iw, ih, ir, it) : ((xx >= ix && xx < ix + iw && yy >= iy && yy < iy + ih) ? 255 : 0);
				cov -= icov;
			}
			if (cov > 0)
				pixel(s, xx, yy, color, cov * alpha / 255);
		}
	}
}

void gradientRoundRect(Surface &s, int x, int y, int w, int h, int r, uint16_t top, uint16_t bottom) {
	if (w <= 0 || h <= 0)
		return;
	r = clampRadius(r, w, h);
	const uint8_t *table = cornerTable(r);
	int y0 = y < s.cy0 ? s.cy0 : y;
	int y1 = y + h > s.cy1 ? s.cy1 : y + h;
	int x0 = x < s.cx0 ? s.cx0 : x;
	int x1 = x + w > s.cx1 ? s.cx1 : x + w;
	for (int yy = y0; yy < y1; yy++) {
		int cr, cg, cb;
		lerp8(top, bottom, h > 1 ? (yy - y) * 255 / (h - 1) : 0, cr, cg, cb);
		uint16_t c = dither(cr, cg, cb, 0, yy);
		uint16_t c1 = dither(cr, cg, cb, 1, yy);
		bool corner = (yy < y + r) || (yy >= y + h - r);
		int sx0 = corner ? x + r : x, sx1 = corner ? x + w - r : x + w;
		if (sx0 < x0)
			sx0 = x0;
		if (sx1 > x1)
			sx1 = x1;
		uint16_t *p = &s.px[yy * s.w];
		for (int xx = sx0; xx < sx1; xx++)
			p[xx] = (xx & 1) ? c1 : c;
		if (!corner || r == 0)
			continue;
		for (int i = 0; i < r; i++) {
			pixel(s, x + i, yy, c, rrCoverage(x + i, yy, x, y, w, h, r, table));
			pixel(s, x + w - 1 - i, yy, c, rrCoverage(x + w - 1 - i, yy, x, y, w, h, r, table));
		}
	}
}

void fillCircle(Surface &s, int cx, int cy, int r, uint16_t color, int alpha) {
	fillRoundRect(s, cx - r, cy - r, r * 2, r * 2, r, color, alpha);
}

void strokeCircle(Surface &s, int cx, int cy, int r, int thickness, uint16_t color, int alpha) {
	strokeRoundRect(s, cx - r, cy - r, r * 2, r * 2, r, thickness, color, alpha);
}

void line(Surface &s, int x0, int y0, int x1, int y1, uint16_t color) {
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	while (true) {
		pixel(s, x0, y0, color);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

void shadow(Surface &s, int x, int y, int w, int h, int r, int spread, int alpha) {
	for (int i = spread; i >= 1; i--)
		fillRoundRect(s, x - i, y - i + 2, w + 2 * i, h + 2 * i, r + i, 0x8000, alpha / (spread + 1));
}

void mask(Surface &s, const uint8_t *m, int mw, int mh, int x, int y, uint16_t color, int alpha) {
	for (int j = 0; j < mh; j++) {
		int yy = y + j;
		if (yy < s.cy0 || yy >= s.cy1)
			continue;
		const uint8_t *row = m + j * mw;
		for (int i = 0; i < mw; i++) {
			int a = row[i];
			if (a)
				pixel(s, x + i, yy, color, alpha >= 255 ? a : a * alpha / 255);
		}
	}
}

void blit(Surface &s, const uint16_t *img, int iw, int ih, int x, int y) {
	for (int j = 0; j < ih; j++) {
		int yy = y + j;
		if (yy < s.cy0 || yy >= s.cy1)
			continue;
		for (int i = 0; i < iw; i++) {
			int xx = x + i;
			if (xx < s.cx0 || xx >= s.cx1)
				continue;
			uint16_t c = img[j * iw + i];
			if (c & 0x8000)
				s.px[yy * s.w + xx] = c;
		}
	}
}

void blitScaled(Surface &s, const uint16_t *img, int iw, int ih, int x, int y, int w, int h) {
	if (w <= 0 || h <= 0)
		return;
	uint32_t stepX = ((uint32_t)iw << 16) / w;
	uint32_t stepY = ((uint32_t)ih << 16) / h;
	for (int j = 0; j < h; j++) {
		int yy = y + j;
		if (yy < s.cy0 || yy >= s.cy1)
			continue;
		const uint16_t *row = img + ((j * stepY) >> 16) * iw;
		uint32_t fx = 0;
		for (int i = 0; i < w; i++, fx += stepX) {
			int xx = x + i;
			if (xx < s.cx0 || xx >= s.cx1)
				continue;
			uint16_t c = row[fx >> 16];
			if (c & 0x8000)
				s.px[yy * s.w + xx] = c;
		}
	}
}

} // namespace gfx

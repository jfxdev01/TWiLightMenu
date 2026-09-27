#include "font.h"

#include <string.h>

#include "font_body_bin.h"
#include "font_bold_bin.h"
#include "font_head_bin.h"
#include "font_huge_bin.h"
#include "font_title_bin.h"

namespace fonts {
Font body, bold, title, head, huge;

bool init() {
	bool ok = body.load(font_body_bin, font_body_bin_size);
	ok &= bold.load(font_bold_bin, font_bold_bin_size);
	ok &= title.load(font_title_bin, font_title_bin_size);
	ok &= head.load(font_head_bin, font_head_bin_size);
	ok &= huge.load(font_huge_bin, font_huge_bin_size);
	// The big fonts only have Latin-1
	title.setFallback(&bold);
	head.setFallback(&bold);
	huge.setFallback(&bold);
	return ok;
}
} // namespace fonts

uint32_t utf8Next(const char *&p, const char *end) {
	unsigned char c = (unsigned char)*p++;
	if (c < 0x80)
		return c;
	int extra = 0;
	uint32_t cp = 0;
	if ((c & 0xE0) == 0xC0) {
		cp = c & 0x1F;
		extra = 1;
	} else if ((c & 0xF0) == 0xE0) {
		cp = c & 0x0F;
		extra = 2;
	} else if ((c & 0xF8) == 0xF0) {
		cp = c & 0x07;
		extra = 3;
	} else {
		return 0xFFFD;
	}
	for (int i = 0; i < extra; i++) {
		if (p >= end || (((unsigned char)*p) & 0xC0) != 0x80)
			return 0xFFFD;
		cp = (cp << 6) | (((unsigned char)*p++) & 0x3F);
	}
	return cp;
}

void utf8Append(std::string &out, uint32_t cp) {
	if (cp < 0x80) {
		out += (char)cp;
	} else if (cp < 0x800) {
		out += (char)(0xC0 | (cp >> 6));
		out += (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out += (char)(0xE0 | (cp >> 12));
		out += (char)(0x80 | ((cp >> 6) & 0x3F));
		out += (char)(0x80 | (cp & 0x3F));
	} else {
		out += (char)(0xF0 | (cp >> 18));
		out += (char)(0x80 | ((cp >> 12) & 0x3F));
		out += (char)(0x80 | ((cp >> 6) & 0x3F));
		out += (char)(0x80 | (cp & 0x3F));
	}
}

bool Font::load(const void *data, size_t size) {
	const uint8_t *d = (const uint8_t *)data;
	if (size < 8 || memcmp(d, "HFN4", 4) != 0)
		return false;
	_lineHeight = d[4];
	_ascent = d[5];
	_count = d[6] | (d[7] << 8);
	_codepoints = (const uint16_t *)(d + 8);
	size_t off = 8 + _count * 2;
	off = (off + 3) & ~3;
	_glyphs = (const Glyph *)(d + off);
	_data = d + off + _count * sizeof(Glyph);
	_fallback = find(0xFFFD);
	if (_fallback < 0)
		_fallback = find('?');
	return true;
}

int Font::find(uint32_t cp) const {
	int lo = 0, hi = _count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		uint32_t v = _codepoints[mid];
		if (v == cp)
			return mid;
		if (v < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

static inline uint32_t normalise(uint32_t cp) {
	// Map typographic spaces to a normal space
	if (cp == 0xA0 || (cp >= 0x2000 && cp <= 0x200A) || cp == '\t')
		return ' ';
	return cp;
}

int Font::advance(uint32_t cp) const {
	cp = normalise(cp);
	if (cp == 0x200B || cp == 0xAD || cp == '\r' || cp == '\n')
		return 0;
	int i = find(cp);
	if (i < 0 && _fallbackFont)
		return _fallbackFont->advance(cp);
	if (i < 0)
		i = _fallback;
	return i < 0 ? 0 : _glyphs[i].adv;
}

int Font::width(const char *utf8, int len) const {
	if (!_count)
		return 0;
	const char *p = utf8;
	const char *end = len < 0 ? utf8 + strlen(utf8) : utf8 + len;
	int w = 0;
	while (p < end)
		w += advance(utf8Next(p, end));
	return w;
}

int Font::draw(Surface &s, int x, int y, const char *utf8, uint16_t color, int alpha, int len) const {
	if (!_count)
		return x;
	const char *p = utf8;
	const char *end = len < 0 ? utf8 + strlen(utf8) : utf8 + len;
	while (p < end) {
		uint32_t cp = normalise(utf8Next(p, end));
		if (cp == 0x200B || cp == 0xAD || cp == '\r' || cp == '\n')
			continue;
		int i = find(cp);
		if (i < 0 && _fallbackFont) {
			std::string one;
			utf8Append(one, cp);
			// Align the baselines
			x = _fallbackFont->draw(s, x, y + _ascent - _fallbackFont->ascent(), one.c_str(), color, alpha, (int)one.size());
			continue;
		}
		if (i < 0)
			i = _fallback;
		if (i < 0)
			continue;
		const Glyph &g = _glyphs[i];
		if (g.w && x + g.xoff < s.cx1 && x + g.xoff + g.w > s.cx0) {
			const uint8_t *src = _data + g.offset;
			int stride = (g.w + 1) >> 1;
			for (int j = 0; j < g.h; j++) {
				int yy = y + g.yoff + j;
				if (yy < s.cy0 || yy >= s.cy1)
					continue;
				const uint8_t *row = src + j * stride;
				for (int k = 0; k < g.w; k++) {
					int v = (k & 1) ? (row[k >> 1] & 0xF) : (row[k >> 1] >> 4);
					if (v)
						gfx::pixel(s, x + g.xoff + k, yy, color, (v * 17) * alpha / 255);
				}
			}
		}
		x += g.adv;
	}
	return x;
}

void Font::drawCentered(Surface &s, int cx, int y, const std::string &t, uint16_t color, int alpha) const {
	draw(s, cx - width(t) / 2, y, t, color, alpha);
}

void Font::drawRight(Surface &s, int rx, int y, const std::string &t, uint16_t color, int alpha) const {
	draw(s, rx - width(t), y, t, color, alpha);
}

std::string Font::ellipsize(const std::string &t, int maxWidth) const {
	if (width(t) <= maxWidth)
		return t;
	static const char kDots[] = "\xE2\x80\xA6"; // …
	int dots = width(kDots);
	const char *p = t.c_str();
	const char *end = p + t.size();
	int w = 0;
	const char *cut = p;
	while (p < end) {
		const char *prev = p;
		int a = advance(utf8Next(p, end));
		if (w + a + dots > maxWidth) {
			cut = prev;
			break;
		}
		w += a;
		cut = p;
	}
	return std::string(t.c_str(), cut - t.c_str()) + kDots;
}

void Font::drawEllipsized(Surface &s, int x, int y, const std::string &t, int maxWidth, uint16_t color, int alpha) const {
	draw(s, x, y, ellipsize(t, maxWidth), color, alpha);
}

void Font::wrap(const std::string &t, int maxWidth, std::vector<std::string> &lines) const {
	const char *p = t.c_str();
	const char *end = p + t.size();
	while (p <= end) {
		// Find the end of the paragraph
		const char *nl = (const char *)memchr(p, '\n', end - p);
		const char *pend = nl ? nl : end;
		if (p == pend) {
			lines.emplace_back();
		}
		while (p < pend) {
			const char *q = p;
			const char *lastBreak = nullptr;
			int w = 0;
			while (q < pend) {
				const char *prev = q;
				uint32_t cp = utf8Next(q, pend);
				int a = advance(cp);
				if (w + a > maxWidth && prev > p) {
					q = prev;
					break;
				}
				w += a;
				if (cp == ' ' || cp == '-' || cp == '/' || cp == 0x200B)
					lastBreak = q;
			}
			const char *lineEnd = q;
			if (q < pend && lastBreak && lastBreak > p)
				lineEnd = lastBreak;
			std::string line(p, lineEnd - p);
			while (!line.empty() && line.back() == ' ')
				line.pop_back();
			lines.push_back(line);
			p = lineEnd;
			while (p < pend && *p == ' ')
				p++;
		}
		if (!nl)
			break;
		p = nl + 1;
		if (p == end) {
			break;
		}
	}
}

int Font::drawWrapped(Surface &s, int x, int y, const std::string &t, int maxWidth, uint16_t color, int maxLines) const {
	std::vector<std::string> lines;
	wrap(t, maxWidth, lines);
	int n = (int)lines.size();
	if (maxLines > 0 && n > maxLines) {
		n = maxLines;
		lines[n - 1] = ellipsize(lines[n - 1] + " \xE2\x80\xA6", maxWidth);
	}
	for (int i = 0; i < n; i++)
		draw(s, x, y + i * _lineHeight, lines[i], color);
	return n * _lineHeight;
}

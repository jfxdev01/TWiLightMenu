#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "i18n.h"
#include "icons_bin.h"
#include "net.h"
#include "platform.h"

namespace ui {

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

static Palette palette;

const Palette &pal() { return palette; }

void applyTheme(bool dark) {
	if (dark) {
		palette.bg = HEXCOLOR(0x2D2D2D);
		palette.bgTop = HEXCOLOR(0x333333);
		palette.panel = HEXCOLOR(0x3F3F3F);
		palette.panelAlt = HEXCOLOR(0x4A4A4A);
		palette.text = HEXCOLOR(0xFFFFFF);
		palette.textDim = HEXCOLOR(0xB4B4B4);
		palette.accent = HEXCOLOR(0x1FD6F0);
		palette.accentText = HEXCOLOR(0x1A1A1A);
		palette.divider = HEXCOLOR(0x5A5A5A);
	} else {
		palette.bg = HEXCOLOR(0xEBEBEB);
		palette.bgTop = HEXCOLOR(0xF5F5F5);
		palette.panel = HEXCOLOR(0xFFFFFF);
		palette.panelAlt = HEXCOLOR(0xDCDCDC);
		palette.text = HEXCOLOR(0x2D2D2D);
		palette.textDim = HEXCOLOR(0x6E6E6E);
		palette.accent = HEXCOLOR(0x0AB4E6);
		palette.accentText = HEXCOLOR(0xFFFFFF);
		palette.divider = HEXCOLOR(0xC8C8C8);
	}
	palette.danger = HEXCOLOR(0xFF4B4B);
	palette.success = HEXCOLOR(0x3CC864);
	palette.warning = HEXCOLOR(0xFFB400);
	palette.dark = dark;
}

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------

static const uint8_t *iconBase;
static int iconSizes[8];
static int iconSizeCount;
static uint32_t iconStride; // bytes per icon (all sizes)
static uint32_t iconSizeOffset[8];

bool initIcons() {
	const uint8_t *d = icons_bin;
	if (memcmp(d, "HICO", 4) != 0)
		return false;
	int count = d[4] | (d[5] << 8);
	iconSizeCount = d[6] | (d[7] << 8);
	uint32_t off = 8;
	uint32_t acc = 0;
	for (int i = 0; i < iconSizeCount && i < 8; i++) {
		iconSizes[i] = d[off] | (d[off + 1] << 8);
		off += 2;
		iconSizeOffset[i] = acc;
		acc += iconSizes[i] * iconSizes[i];
	}
	off = (off + 3) & ~3;
	iconStride = acc;
	iconBase = d + off;
	return count == ICON_COUNT;
}

int iconSize(IconSize size) { return iconSizes[size]; }

void icon(Surface &s, IconId id, IconSize size, int x, int y, uint16_t color, int alpha) {
	if (!iconBase || id >= ICON_COUNT)
		return;
	int px = iconSizes[size];
	gfx::mask(s, iconBase + id * iconStride + iconSizeOffset[size], px, px, x, y, color, alpha);
}

void appTile(Surface &s, const Rect &r, IconId id, uint16_t color, bool large) {
	int radius = r.w / 6;
	gfx::gradientRoundRect(s, r.x, r.y, r.w, r.h, radius, gfx::shade(color, 40), gfx::shade(color, -30));
	IconSize sz = large ? ICONSIZE_64 : ICONSIZE_40;
	if (!large && r.w < 48)
		sz = ICONSIZE_24;
	int px = iconSizes[sz];
	icon(s, id, sz, r.x + (r.w - px) / 2, r.y + (r.h - px) / 2, 0xFFFF);
}

// ---------------------------------------------------------------------------
// Chrome
// ---------------------------------------------------------------------------

// The background never changes, so it's rendered once per theme and copied
static uint16_t bgCache[256 * 192];
static bool bgCacheDark = false, bgCacheValid = false;

void background(Surface &s) {
	if (!bgCacheValid || bgCacheDark != palette.dark) {
		Surface c;
		c.init(bgCache, 256, 192);
		gfx::gradientV(c, 0, 0, 256, 192, palette.bgTop, palette.bg);
		bgCacheValid = true;
		bgCacheDark = palette.dark;
	}
	if (s.w == 256 && s.h == 192 && s.cx0 == 0 && s.cy0 == 0 && s.cx1 == 256 && s.cy1 == 192)
		memcpy(s.px, bgCache, sizeof(bgCache));
	else
		gfx::gradientV(s, 0, 0, s.w, s.h, palette.bgTop, palette.bg);
}

void wifiBars(Surface &s, int x, int y, int bars, uint16_t color) {
	// 3 arcs approximated with rounded bars of increasing height
	for (int i = 0; i < 3; i++) {
		int h = 4 + i * 3;
		bool on = bars > i;
		gfx::fillRoundRect(s, x + i * 5, y + 10 - h, 3, h, 1, color, on ? 255 : 70);
	}
	if (bars < 0)
		gfx::line(s, x, y + 10, x + 13, y, color);
}

void battery(Surface &s, int x, int y, int percent, bool charging, uint16_t color) {
	gfx::strokeRoundRect(s, x, y, 20, 11, 3, 1, color);
	gfx::fillRect(s, x + 20, y + 3, 2, 5, color);
	int w = percent * 16 / 100;
	if (w < 1)
		w = 1;
	uint16_t fill = percent <= 15 ? palette.danger : (charging ? palette.success : color);
	gfx::fillRoundRect(s, x + 2, y + 2, w, 7, 1, fill);
}

void statusBar(Surface &top) {
	const Palette &p = palette;
	// User "avatar": favourite colour circle with the first letter
	std::string name = platform::nickname();
	uint16_t fav = platform::favoriteColor();
	gfx::fillCircle(top, 15, 13, 10, fav);
	gfx::strokeCircle(top, 15, 13, 11, 1, p.text, 60);
	if (!name.empty()) {
		const char *q = name.c_str();
		uint32_t cp = utf8Next(q, name.c_str() + name.size());
		std::string first;
		utf8Append(first, cp >= 'a' && cp <= 'z' ? cp - 32 : cp);
		fonts::bold.drawCentered(top, 15, 13 - fonts::bold.lineHeight() / 2, first, 0xFFFF);
	}
	fonts::body.draw(top, 31, 13 - fonts::body.lineHeight() / 2, name, p.text);

	// Right side: time, WiFi, battery
	int x = 256 - 6;
	int level = platform::batteryLevel();
	battery(top, x - 22, 8, level, platform::charging(), p.text);
	x -= 26;
	char buf[16];
	snprintf(buf, sizeof(buf), "%d%%", level);
	x -= fonts::body.width(buf);
	fonts::body.draw(top, x, 13 - fonts::body.lineHeight() / 2, buf, p.text);
	x -= 20;
	wifiBars(top, x, 7, net::signalBars(), p.text);
	x -= 6;
	time_t t = platform::now();
	struct tm *tm = localtime(&t);
	snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
	x -= fonts::bold.width(buf);
	fonts::bold.draw(top, x, 13 - fonts::bold.lineHeight() / 2, buf, p.text);

	gfx::hline(top, 8, 27, 240, p.divider);
}

int titleBar(Surface &top, IconId id, uint16_t color, const std::string &title, const std::string &subtitle) {
	appTile(top, {10, 34, 36, 36}, id, color, false);
	int ty = subtitle.empty() ? 52 - fonts::head.lineHeight() / 2 : 34;
	fonts::head.drawEllipsized(top, 54, ty, title, 196, palette.text);
	if (!subtitle.empty())
		fonts::body.drawEllipsized(top, 55, 58, subtitle, 194, palette.textDim);
	return 78;
}

static std::vector<std::pair<Rect, uint32_t>> footerRects;
static uint32_t footerTappedKey;

static void drawButtonGlyph(Surface &s, int x, int cy, const std::string &b) {
	const Palette &p = palette;
	int w = fonts::bold.width(b);
	int bw = b.size() > 1 ? w + 10 : 15;
	gfx::fillRoundRect(s, x, cy - 7, bw, 15, 7, p.text);
	fonts::bold.draw(s, x + (bw - w) / 2, cy - fonts::bold.lineHeight() / 2, b, p.bg);
}

void footer(Surface &bottom, const std::vector<Hint> &hints, const std::string &left) {
	const Palette &p = palette;
	gfx::fillRect(bottom, 0, 168, 256, 24, p.bg);
	gfx::hline(bottom, 8, 168, 240, p.divider);
	footerRects.clear();
	int x = 250;
	for (int i = (int)hints.size() - 1; i >= 0; i--) {
		const Hint &h = hints[i];
		int lw = fonts::body.width(h.label);
		int gw = h.button.size() > 1 ? fonts::bold.width(h.button) + 10 : 15;
		int total = gw + 4 + lw;
		x -= total;
		drawButtonGlyph(bottom, x, 180, h.button);
		fonts::body.draw(bottom, x + gw + 4, 180 - fonts::body.lineHeight() / 2, h.label, p.text);
		footerRects.push_back({{x - 4, 169, total + 8, 23}, h.key});
		x -= 12;
	}
	if (!left.empty() && x - 12 >= 48)
		fonts::body.drawEllipsized(bottom, 8, 180 - fonts::body.lineHeight() / 2, left, x - 12, p.textDim);

	footerTappedKey = 0;
	for (auto &fr : footerRects) {
		if (tapped(fr.first))
			footerTappedKey = fr.second;
	}
}

uint32_t footerTapped() {
	uint32_t k = footerTappedKey;
	footerTappedKey = 0;
	return k;
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

void card(Surface &s, const Rect &r, int radius) {
	if (!palette.dark)
		gfx::shadow(s, r.x, r.y, r.w, r.h, radius, 3, 60);
	gfx::fillRoundRect(s, r.x, r.y, r.w, r.h, radius, palette.panel);
}

void cursorFrame(Surface &s, const Rect &r, int radius) {
	// Pulses between the accent colour and a lighter tint, like the Switch
	int t = platform::frame() % 60;
	int k = t < 30 ? t : 60 - t;
	uint16_t c = gfx::mix(palette.accent, 0xFFFF, k * 3);
	gfx::strokeRoundRect(s, r.x - 5, r.y - 5, r.w + 10, r.h + 10, radius + 5, 3, c);
}

void button(Surface &s, const Rect &r, const std::string &label, bool focused, IconId id, bool primary) {
	const Palette &p = palette;
	uint16_t bg = primary ? p.accent : p.panel;
	uint16_t fg = primary ? p.accentText : p.text;
	if (!p.dark && !primary)
		gfx::shadow(s, r.x, r.y, r.w, r.h, 8, 2, 50);
	gfx::fillRoundRect(s, r.x, r.y, r.w, r.h, 8, bg);
	int lw = fonts::bold.width(label);
	int iw = id < ICON_COUNT ? 20 : 0;
	int x = r.x + (r.w - lw - iw) / 2;
	if (id < ICON_COUNT) {
		icon(s, id, ICONSIZE_16, x, r.y + (r.h - 16) / 2, fg);
		x += iw;
	}
	fonts::bold.draw(s, x, r.y + (r.h - fonts::bold.lineHeight()) / 2, label, fg);
	if (focused)
		cursorFrame(s, r, 8);
}

void roundButton(Surface &s, int cx, int cy, int radius, IconId id, bool focused, uint16_t glyphColor, const std::string &text) {
	const Palette &p = palette;
	if (!p.dark)
		gfx::shadow(s, cx - radius, cy - radius, radius * 2, radius * 2, radius, 2, 50);
	gfx::fillCircle(s, cx, cy, radius, p.panel);
	if (!text.empty()) {
		fonts::bold.drawCentered(s, cx, cy - fonts::bold.lineHeight() / 2, text, glyphColor);
	} else if (id < ICON_COUNT) {
		icon(s, id, ICONSIZE_16, cx - 8, cy - 8, glyphColor);
	}
	if (focused)
		cursorFrame(s, {cx - radius, cy - radius, radius * 2, radius * 2}, radius);
}

void spinner(Surface &s, int cx, int cy, int radius) {
	int f = platform::frame() / 4;
	for (int i = 0; i < 8; i++) {
		// 8 dots on a circle, the "head" is brightest
		static const int8_t dx[8] = {0, 7, 10, 7, 0, -7, -10, -7};
		static const int8_t dy[8] = {-10, -7, 0, 7, 10, 7, 0, -7};
		int age = (f - i) & 7;
		gfx::fillCircle(s, cx + dx[i] * radius / 10, cy + dy[i] * radius / 10, radius / 5 + 1, palette.accent, 255 - age * 28);
	}
}

void progressBar(Surface &s, const Rect &r, int percent) {
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	gfx::fillRoundRect(s, r.x, r.y, r.w, r.h, r.h / 2, palette.panelAlt);
	int w = r.w * percent / 100;
	if (w >= r.h)
		gfx::fillRoundRect(s, r.x, r.y, w, r.h, r.h / 2, palette.accent);
}

void toggle(Surface &s, int x, int y, bool on) {
	gfx::fillRoundRect(s, x, y, 30, 16, 8, on ? palette.accent : palette.panelAlt);
	gfx::fillCircle(s, on ? x + 22 : x + 8, y + 8, 6, 0xFFFF);
}

// ---------------------------------------------------------------------------
// Touch helpers
// ---------------------------------------------------------------------------

static int pressX = -1, pressY = -1;

bool pressedIn(const Rect &r) {
	return platform::touchDown() && r.contains(platform::touchX(), platform::touchY());
}

bool tapped(const Rect &r) {
	if (platform::touchDown()) {
		pressX = platform::touchX();
		pressY = platform::touchY();
	}
	if (!platform::touchReleased())
		return false;
	return r.contains(pressX, pressY) && r.contains(platform::touchX(), platform::touchY());
}

// ---------------------------------------------------------------------------
// Toast
// ---------------------------------------------------------------------------

static std::string toastText;
static int toastFrames = 0;

void toast(const std::string &text, int frames) {
	toastText = text;
	toastFrames = frames;
}

void drawToast(Surface &top) {
	if (toastFrames <= 0)
		return;
	toastFrames--;
	int alpha = toastFrames > 20 ? 255 : toastFrames * 12;
	int w = fonts::body.width(toastText) + 24;
	if (w > 240)
		w = 240;
	int x = (256 - w) / 2;
	gfx::fillRoundRect(top, x, 160, w, 24, 12, palette.dark ? HEXCOLOR(0xEEEEEE) : HEXCOLOR(0x333333), alpha * 235 / 255);
	fonts::body.drawEllipsized(top, x + 12, 172 - fonts::body.lineHeight() / 2, toastText, w - 24,
	                           palette.dark ? HEXCOLOR(0x222222) : 0xFFFF, alpha);
}

// ---------------------------------------------------------------------------
// Modal dialogs
// ---------------------------------------------------------------------------

static uint16_t snapTop[256 * 192];
static uint16_t snapBottom[256 * 192];

void snapshot() {
	memcpy(snapTop, platform::top().px, sizeof(snapTop));
	memcpy(snapBottom, platform::bottom().px, sizeof(snapBottom));
}

void restoreSnapshot() {
	memcpy(platform::top().px, snapTop, sizeof(snapTop));
	memcpy(platform::bottom().px, snapBottom, sizeof(snapBottom));
}

static void dimmed() {
	restoreSnapshot();
	gfx::fillRect(platform::top(), 0, 0, 256, 192, 0x8000, 150);
	gfx::fillRect(platform::bottom(), 0, 0, 256, 192, 0x8000, 150);
}

static void dialogCard(const std::string &title, const std::string &text) {
	Surface &top = platform::top();
	std::vector<std::string> lines;
	fonts::body.wrap(text, 208, lines);
	if (lines.size() > 7)
		lines.resize(7);
	int h = 44 + (int)lines.size() * fonts::body.lineHeight();
	int y = (192 - h) / 2;
	card(top, {12, y, 232, h}, 12);
	fonts::title.drawCentered(top, 128, y + 12, fonts::title.ellipsize(title, 208), palette.text);
	for (size_t i = 0; i < lines.size(); i++)
		fonts::body.drawCentered(top, 128, y + 36 + i * fonts::body.lineHeight(), lines[i], palette.textDim);
}

void message(const std::string &title, const std::string &text) {
	snapshot();
	while (true) {
		platform::scanInput();
		dimmed();
		dialogCard(title, text);
		Rect ok{64, 76, 128, 36};
		button(platform::bottom(), ok, "OK", true, ICON_COUNT, true);
		footer(platform::bottom(), {{"A", "OK", KEY_A}});
		uint32_t k = platform::keysDown() | footerTapped();
		if ((k & (KEY_A | KEY_B)) || tapped(ok)) {
			platform::playSfx(platform::SFX_SELECT);
			break;
		}
		platform::present();
	}
	restoreSnapshot();
}

bool confirm(const std::string &title, const std::string &text, const std::string &yes, const std::string &no) {
	snapshot();
	int focus = 1;
	bool result = false;
	while (true) {
		platform::scanInput();
		dimmed();
		dialogCard(title, text);
		Rect rn{16, 76, 108, 36}, ry{132, 76, 108, 36};
		button(platform::bottom(), rn, no, focus == 0);
		button(platform::bottom(), ry, yes, focus == 1, ICON_COUNT, true);
		footer(platform::bottom(), {{"B", no, KEY_B}, {"A", TR("Select", "Selecionar"), KEY_A}});
		uint32_t k = platform::keysDown() | footerTapped();
		if (k & (KEY_LEFT | KEY_RIGHT)) {
			focus ^= 1;
			platform::playSfx(platform::SFX_MOVE);
		}
		if (tapped(rn)) {
			result = false;
			break;
		}
		if (tapped(ry)) {
			result = true;
			break;
		}
		if (k & KEY_A) {
			result = focus == 1;
			break;
		}
		if (k & KEY_B) {
			result = false;
			break;
		}
		platform::present();
	}
	platform::playSfx(result ? platform::SFX_SELECT : platform::SFX_BACK);
	restoreSnapshot();
	return result;
}

int choose(const std::string &title, const std::vector<std::string> &options, int current) {
	snapshot();
	ListView list;
	list.area = {0, 0, 256, 168};
	list.itemHeight = 30;
	for (const std::string &o : options) {
		ListItem it;
		it.title = o;
		list.items.push_back(it);
	}
	list.selected = current >= 0 && current < (int)options.size() ? current : 0;
	list.ensureVisible();
	int result = -1;
	while (true) {
		platform::scanInput();
		dimmed();
		dialogCard(title, TR("Choose an option below.", "Escolha uma opção abaixo."));
		Surface &b = platform::bottom();
		background(b);
		int r = list.update();
		list.draw(b);
		footer(b, {{"B", TR("Cancel", "Cancelar"), KEY_B}, {"A", "OK", KEY_A}});
		uint32_t k = platform::keysDown() | footerTapped();
		if (r >= 0) {
			result = r;
			platform::playSfx(platform::SFX_SELECT);
			break;
		}
		if (k & KEY_B) {
			platform::playSfx(platform::SFX_BACK);
			break;
		}
		platform::present();
	}
	restoreSnapshot();
	return result;
}

void busy(const std::string &title, const std::string &text, int percent, bool cancellable) {
	Surface &top = platform::top();
	Surface &bottom = platform::bottom();
	background(top);
	statusBar(top);
	card(top, {16, 52, 224, 104}, 12);
	fonts::title.drawCentered(top, 128, 64, fonts::title.ellipsize(title, 200), palette.text);
	std::vector<std::string> lines;
	fonts::body.wrap(text, 200, lines);
	for (size_t i = 0; i < lines.size() && i < 2; i++)
		fonts::body.drawCentered(top, 128, 88 + i * fonts::body.lineHeight(), lines[i], palette.textDim);
	if (percent >= 0) {
		progressBar(top, {36, 132, 184, 8}, percent);
	} else {
		spinner(top, 128, 136, 10);
	}
	background(bottom);
	spinner(bottom, 128, 80, 16);
	if (cancellable)
		footer(bottom, {{"B", TR("Cancel", "Cancelar"), KEY_B}});
	else
		footer(bottom, {});
	platform::present();
}

// ---------------------------------------------------------------------------
// On-screen keyboard
// ---------------------------------------------------------------------------

namespace {

enum KeyAction { K_CHAR, K_SHIFT, K_BACK, K_LAYER, K_SPACE, K_OK, K_ACCENT };

struct Key {
	const char *label; // UTF-8, or the character itself
	KeyAction action;
	int width; // in units of 1/2 key
};

// Rows of the three layers. Each row is 20 half-units wide.
const Key kLetters[5][11] = {
	{{"1", K_CHAR, 2}, {"2", K_CHAR, 2}, {"3", K_CHAR, 2}, {"4", K_CHAR, 2}, {"5", K_CHAR, 2}, {"6", K_CHAR, 2}, {"7", K_CHAR, 2}, {"8", K_CHAR, 2}, {"9", K_CHAR, 2}, {"0", K_CHAR, 2}, {nullptr, K_CHAR, 0}},
	{{"q", K_CHAR, 2}, {"w", K_CHAR, 2}, {"e", K_CHAR, 2}, {"r", K_CHAR, 2}, {"t", K_CHAR, 2}, {"y", K_CHAR, 2}, {"u", K_CHAR, 2}, {"i", K_CHAR, 2}, {"o", K_CHAR, 2}, {"p", K_CHAR, 2}, {nullptr, K_CHAR, 0}},
	{{"a", K_CHAR, 2}, {"s", K_CHAR, 2}, {"d", K_CHAR, 2}, {"f", K_CHAR, 2}, {"g", K_CHAR, 2}, {"h", K_CHAR, 2}, {"j", K_CHAR, 2}, {"k", K_CHAR, 2}, {"l", K_CHAR, 2}, {"-", K_CHAR, 2}, {nullptr, K_CHAR, 0}},
	{{"\xE2\x86\x91", K_SHIFT, 3}, {"z", K_CHAR, 2}, {"x", K_CHAR, 2}, {"c", K_CHAR, 2}, {"v", K_CHAR, 2}, {"b", K_CHAR, 2}, {"n", K_CHAR, 2}, {"m", K_CHAR, 2}, {"\xE2\x86\x90", K_BACK, 3}, {nullptr, K_CHAR, 0}},
	{{"#+=", K_LAYER, 3}, {"\xC3\xA1\xC3\xA7", K_ACCENT, 3}, {"/", K_CHAR, 2}, {" ", K_SPACE, 6}, {".", K_CHAR, 2}, {"OK", K_OK, 4}, {nullptr, K_CHAR, 0}},
};

const Key kSymbols[5][11] = {
	{{"!", K_CHAR, 2}, {"@", K_CHAR, 2}, {"#", K_CHAR, 2}, {"$", K_CHAR, 2}, {"%", K_CHAR, 2}, {"&", K_CHAR, 2}, {"*", K_CHAR, 2}, {"(", K_CHAR, 2}, {")", K_CHAR, 2}, {"=", K_CHAR, 2}, {nullptr, K_CHAR, 0}},
	{{"_", K_CHAR, 2}, {"+", K_CHAR, 2}, {"[", K_CHAR, 2}, {"]", K_CHAR, 2}, {"{", K_CHAR, 2}, {"}", K_CHAR, 2}, {"<", K_CHAR, 2}, {">", K_CHAR, 2}, {"|", K_CHAR, 2}, {"\\", K_CHAR, 2}, {nullptr, K_CHAR, 0}},
	{{";", K_CHAR, 2}, {":", K_CHAR, 2}, {"'", K_CHAR, 2}, {"\"", K_CHAR, 2}, {",", K_CHAR, 2}, {"?", K_CHAR, 2}, {"~", K_CHAR, 2}, {"^", K_CHAR, 2}, {"`", K_CHAR, 2}, {"\xE2\x82\xAC", K_CHAR, 2}, {nullptr, K_CHAR, 0}},
	{{"\xE2\x86\x91", K_SHIFT, 3}, {".com", K_CHAR, 4}, {".br", K_CHAR, 3}, {"www.", K_CHAR, 4}, {"https://", K_CHAR, 3}, {"\xE2\x86\x90", K_BACK, 3}, {nullptr, K_CHAR, 0}},
	{{"abc", K_LAYER, 3}, {"\xC3\xA1\xC3\xA7", K_ACCENT, 3}, {"/", K_CHAR, 2}, {" ", K_SPACE, 6}, {".", K_CHAR, 2}, {"OK", K_OK, 4}, {nullptr, K_CHAR, 0}},
};

const Key kAccents[5][11] = {
	{{"\xC3\xA1", K_CHAR, 2}, {"\xC3\xA0", K_CHAR, 2}, {"\xC3\xA2", K_CHAR, 2}, {"\xC3\xA3", K_CHAR, 2}, {"\xC3\xA4", K_CHAR, 2}, {"\xC3\xA9", K_CHAR, 2}, {"\xC3\xA8", K_CHAR, 2}, {"\xC3\xAA", K_CHAR, 2}, {"\xC3\xAB", K_CHAR, 2}, {"\xC3\xAD", K_CHAR, 2}, {nullptr, K_CHAR, 0}},
	{{"\xC3\xAC", K_CHAR, 2}, {"\xC3\xAE", K_CHAR, 2}, {"\xC3\xB3", K_CHAR, 2}, {"\xC3\xB2", K_CHAR, 2}, {"\xC3\xB4", K_CHAR, 2}, {"\xC3\xB5", K_CHAR, 2}, {"\xC3\xB6", K_CHAR, 2}, {"\xC3\xBA", K_CHAR, 2}, {"\xC3\xB9", K_CHAR, 2}, {"\xC3\xBC", K_CHAR, 2}, {nullptr, K_CHAR, 0}},
	{{"\xC3\xA7", K_CHAR, 2}, {"\xC3\xB1", K_CHAR, 2}, {"\xC3\x9F", K_CHAR, 2}, {"\xC2\xBF", K_CHAR, 2}, {"\xC2\xA1", K_CHAR, 2}, {"\xC2\xBA", K_CHAR, 2}, {"\xC2\xAA", K_CHAR, 2}, {"\xC2\xB0", K_CHAR, 2}, {"\xE2\x80\x93", K_CHAR, 2}, {"\xE2\x80\xA6", K_CHAR, 2}, {nullptr, K_CHAR, 0}},
	{{"\xE2\x86\x91", K_SHIFT, 3}, {"\xC2\xAB", K_CHAR, 2}, {"\xC2\xBB", K_CHAR, 2}, {"\xE2\x80\x9C", K_CHAR, 2}, {"\xE2\x80\x9D", K_CHAR, 2}, {"\xC2\xA3", K_CHAR, 2}, {"\xC2\xA5", K_CHAR, 2}, {"\xC2\xA9", K_CHAR, 2}, {"\xE2\x86\x90", K_BACK, 3}, {nullptr, K_CHAR, 0}},
	{{"abc", K_LAYER, 3}, {"#+=", K_ACCENT, 3}, {"/", K_CHAR, 2}, {" ", K_SPACE, 6}, {".", K_CHAR, 2}, {"OK", K_OK, 4}, {nullptr, K_CHAR, 0}},
};

struct PlacedKey {
	const Key *key;
	Rect rect;
	int row, col;
};

std::string upperUtf8(const char *s) {
	std::string out;
	const char *p = s;
	const char *end = s + strlen(s);
	while (p < end) {
		uint32_t cp = utf8Next(p, end);
		if (cp >= 'a' && cp <= 'z')
			cp -= 32;
		else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)
			cp -= 32;
		utf8Append(out, cp);
	}
	return out;
}

void popUtf8(std::string &s) {
	if (s.empty())
		return;
	size_t i = s.size() - 1;
	while (i > 0 && (((unsigned char)s[i]) & 0xC0) == 0x80)
		i--;
	s.erase(i);
}

int utf8Length(const std::string &s) {
	int n = 0;
	for (unsigned char c : s)
		if ((c & 0xC0) != 0x80)
			n++;
	return n;
}

} // namespace

bool keyboard(const std::string &title, std::string &text, int maxLen, bool secret, const std::string &placeholder) {
	std::string value = text;
	int layer = 0; // 0 letters, 1 symbols, 2 accents
	bool shift = false, capsLock = false;
	bool reveal = !secret;
	int curRow = 1, curCol = 0;
	bool accepted = false;

	while (true) {
		platform::scanInput();
		const Key(*rows)[11] = layer == 0 ? kLetters : layer == 1 ? kSymbols : kAccents;

		// Layout
		std::vector<PlacedKey> keys;
		const int unit = 12, gap = 2, top = 8, rowH = 30;
		for (int r = 0; r < 5; r++) {
			int x = 8;
			for (int c = 0; rows[r][c].label; c++) {
				const Key &k = rows[r][c];
				int w = k.width * unit - gap;
				keys.push_back({&k, {x, top + r * rowH, w, rowH - 4}, r, c});
				x += k.width * unit;
			}
		}

		// Input
		const PlacedKey *pressed = nullptr;
		uint32_t kd = platform::keysDown();
		uint32_t kr = platform::keysRepeat();
		auto findKey = [&](int r, int c) -> const PlacedKey * {
			for (const PlacedKey &pk : keys)
				if (pk.row == r && pk.col == c)
					return &pk;
			return nullptr;
		};
		auto rowLen = [&](int r) {
			int n = 0;
			while (rows[r][n].label)
				n++;
			return n;
		};
		if (kr & (KEY_UP | KEY_DOWN)) {
			// Keep the horizontal position when changing rows
			const PlacedKey *cur = findKey(curRow, curCol);
			int cx = cur ? cur->rect.x + cur->rect.w / 2 : 128;
			curRow = (curRow + ((kr & KEY_UP) ? 4 : 1)) % 5;
			int best = 0, bestD = 1000;
			for (const PlacedKey &pk : keys) {
				if (pk.row != curRow)
					continue;
				int d = abs(pk.rect.x + pk.rect.w / 2 - cx);
				if (d < bestD) {
					bestD = d;
					best = pk.col;
				}
			}
			curCol = best;
			platform::playSfx(platform::SFX_MOVE);
		}
		if (kr & KEY_LEFT) {
			curCol = (curCol + rowLen(curRow) - 1) % rowLen(curRow);
			platform::playSfx(platform::SFX_MOVE);
		}
		if (kr & KEY_RIGHT) {
			curCol = (curCol + 1) % rowLen(curRow);
			platform::playSfx(platform::SFX_MOVE);
		}
		if (curCol >= rowLen(curRow))
			curCol = rowLen(curRow) - 1;
		if (kd & KEY_A)
			pressed = findKey(curRow, curCol);
		for (const PlacedKey &pk : keys) {
			if (pressedIn(pk.rect)) {
				pressed = &pk;
				curRow = pk.row;
				curCol = pk.col;
			}
		}
		uint32_t ft = footerTapped();
		if ((kr & KEY_B) || (ft & KEY_B)) {
			if (value.empty() && (kd & KEY_B)) {
				platform::playSfx(platform::SFX_BACK);
				break;
			}
			popUtf8(value);
			platform::playSfx(platform::SFX_MOVE);
		}
		if (kd & KEY_Y) {
			value += ' ';
		}
		if (kd & KEY_L)
			layer = (layer + 1) % 3;
		if (kd & KEY_SELECT)
			shift = !shift;
		if (kd & KEY_X)
			reveal = !reveal;
		if ((kd & KEY_START) || (ft & KEY_START)) {
			accepted = true;
			break;
		}
		if (pressed) {
			const Key &k = *pressed->key;
			switch (k.action) {
				case K_CHAR:
					if (utf8Length(value) < maxLen) {
						value += shift ? upperUtf8(k.label) : std::string(k.label);
						if (shift && !capsLock)
							shift = false;
					}
					platform::playSfx(platform::SFX_MOVE);
					break;
				case K_SPACE:
					if (utf8Length(value) < maxLen)
						value += ' ';
					platform::playSfx(platform::SFX_MOVE);
					break;
				case K_BACK:
					popUtf8(value);
					platform::playSfx(platform::SFX_MOVE);
					break;
				case K_SHIFT:
					if (shift && !capsLock)
						capsLock = true;
					else {
						shift = !shift;
						capsLock = false;
					}
					platform::playSfx(platform::SFX_TOGGLE);
					break;
				case K_LAYER:
					layer = layer == 0 ? 1 : 0;
					platform::playSfx(platform::SFX_TOGGLE);
					break;
				case K_ACCENT:
					layer = layer == 2 ? 1 : 2;
					platform::playSfx(platform::SFX_TOGGLE);
					break;
				case K_OK:
					accepted = true;
					break;
			}
			if (accepted)
				break;
		}

		// Top screen: title and text field
		Surface &ts = platform::top();
		background(ts);
		statusBar(ts);
		fonts::title.draw(ts, 12, 40, title, palette.text);
		Rect field{10, 66, 236, 34};
		gfx::fillRoundRect(ts, field.x, field.y, field.w, field.h, 8, palette.panel);
		gfx::strokeRoundRect(ts, field.x, field.y, field.w, field.h, 8, 2, palette.accent);
		std::string shown = value;
		if (!reveal) {
			shown.clear();
			for (int i = 0; i < utf8Length(value); i++)
				shown += "\xE2\x80\xA2"; // •
		}
		int tw = fonts::body.width(shown);
		int tx = field.x + 10;
		if (tw > field.w - 24)
			tx -= tw - (field.w - 24);
		ts.setClip(field.x + 6, field.y, field.w - 12, field.h);
		if (value.empty() && !placeholder.empty())
			fonts::body.draw(ts, field.x + 10, field.y + 10, placeholder, palette.textDim);
		else
			fonts::body.draw(ts, tx, field.y + 10, shown, palette.text);
		if ((platform::frame() / 30) & 1)
			gfx::fillRect(ts, tx + tw + 1, field.y + 8, 2, 18, palette.accent);
		ts.resetClip();
		char count[32];
		snprintf(count, sizeof(count), "%d/%d", utf8Length(value), maxLen);
		fonts::body.drawRight(ts, 246, 104, count, palette.textDim);
		std::string help = TR("L: symbols · SELECT: shift · Y: space · START: done", "L: símbolos · SELECT: maiúsculas · Y: espaço · START: concluir");
		if (secret)
			help += TR(" · X: show/hide", " · X: mostrar/ocultar");
		fonts::body.drawWrapped(ts, 12, 126, help, 232, palette.textDim, 3);

		// Bottom screen: keys
		Surface &bs = platform::bottom();
		background(bs);
		for (const PlacedKey &pk : keys) {
			const Key &k = *pk.key;
			bool special = k.action != K_CHAR && k.action != K_SPACE;
			bool active = (k.action == K_SHIFT && shift) || (k.action == K_OK);
			uint16_t bg = active ? palette.accent : (special ? palette.panelAlt : palette.panel);
			uint16_t fg = active ? palette.accentText : palette.text;
			bool isPressed = platform::touching() && pk.rect.contains(platform::touchX(), platform::touchY());
			if (isPressed)
				bg = gfx::mix(bg, palette.accent, 120);
			gfx::fillRoundRect(bs, pk.rect.x, pk.rect.y, pk.rect.w, pk.rect.h, 6, bg);
			std::string label = (k.action == K_CHAR && shift) ? upperUtf8(k.label) : std::string(k.label);
			if (k.action == K_SPACE)
				label = TR("space", "espaço");
			const Font &f = (k.action == K_CHAR && strlen(k.label) <= 3) ? fonts::title : fonts::body;
			f.drawCentered(bs, pk.rect.x + pk.rect.w / 2, pk.rect.y + (pk.rect.h - f.lineHeight()) / 2, label, fg);
			if (pk.row == curRow && pk.col == curCol)
				gfx::strokeRoundRect(bs, pk.rect.x - 2, pk.rect.y - 2, pk.rect.w + 4, pk.rect.h + 4, 7, 2, palette.accent);
		}
		footer(bs, {{"B", TR("Delete", "Apagar"), KEY_B}, {"+", TR("Done", "Concluir"), KEY_START}});
		platform::present();
	}
	if (accepted) {
		text = value;
		platform::playSfx(platform::SFX_SELECT);
	}
	return accepted;
}

// ---------------------------------------------------------------------------
// ListView
// ---------------------------------------------------------------------------

void ListView::ensureVisible() {
	int y = selected * itemHeight;
	if (y < _scroll)
		_scroll = y;
	if (y + itemHeight > _scroll + area.h)
		_scroll = y + itemHeight - area.h;
	int maxScroll = (int)items.size() * itemHeight - area.h;
	if (maxScroll < 0)
		maxScroll = 0;
	if (_scroll > maxScroll)
		_scroll = maxScroll;
	if (_scroll < 0)
		_scroll = 0;
}

int ListView::update() {
	if (items.empty())
		return -1;
	int activated = -1;
	uint32_t kr = platform::keysRepeat();
	uint32_t kd = platform::keysDown();
	int n = (int)items.size();
	int page = area.h / itemHeight;
	if (focused) {
		int old = selected;
		if (kr & KEY_UP)
			selected = selected > 0 ? selected - 1 : n - 1;
		if (kr & KEY_DOWN)
			selected = selected < n - 1 ? selected + 1 : 0;
		if (kr & KEY_L)
			selected = selected - page < 0 ? 0 : selected - page;
		if (kr & KEY_R)
			selected = selected + page >= n ? n - 1 : selected + page;
		if (old != selected) {
			platform::playSfx(platform::SFX_MOVE);
			ensureVisible();
		}
		if ((kd & KEY_A) && items[selected].enabled)
			activated = selected;
	}

	// Touch: drag to scroll, tap to activate
	if (platform::touchDown() && area.contains(platform::touchX(), platform::touchY())) {
		_pressed = true;
		_dragging = false;
		_dragStartY = platform::touchY();
		_dragStartScroll = _scroll;
		_pressIndex = (platform::touchY() - area.y + _scroll) / itemHeight;
	}
	if (_pressed && platform::touching()) {
		int dy = platform::touchY() - _dragStartY;
		if (dy > 6 || dy < -6)
			_dragging = true;
		if (_dragging) {
			_scroll = _dragStartScroll - dy;
			int maxScroll = n * itemHeight - area.h;
			if (maxScroll < 0)
				maxScroll = 0;
			if (_scroll < 0)
				_scroll = 0;
			if (_scroll > maxScroll)
				_scroll = maxScroll;
		}
	}
	if (_pressed && platform::touchReleased()) {
		_pressed = false;
		if (!_dragging && _pressIndex >= 0 && _pressIndex < n && items[_pressIndex].enabled) {
			selected = _pressIndex;
			activated = selected;
		}
	}
	return activated;
}

void ListView::draw(Surface &s) {
	const Palette &p = pal();
	s.setClip(area.x, area.y, area.w, area.h);
	int first = _scroll / itemHeight;
	for (int i = first; i < (int)items.size(); i++) {
		int y = area.y + i * itemHeight - _scroll;
		if (y >= area.y + area.h)
			break;
		const ListItem &it = items[i];
		Rect r{area.x + 8, y + 2, area.w - 16, itemHeight - 4};
		bool sel = focused && i == selected;
		bool down = _pressed && !_dragging && i == _pressIndex && platform::touching();
		gfx::fillRoundRect(s, r.x, r.y, r.w, r.h, 6, down ? p.panelAlt : p.panel);
		int tx = r.x + 10;
		if (it.icon < ICON_COUNT) {
			int sz = r.h - 8;
			if (sz > 24)
				sz = 24;
			Rect ir{r.x + 5, r.y + (r.h - sz) / 2, sz, sz};
			if (it.iconColor)
				appTile(s, ir, it.icon, it.iconColor, false);
			else
				icon(s, it.icon, ICONSIZE_16, ir.x + (sz - 16) / 2, ir.y + (sz - 16) / 2, p.text);
			tx = ir.x + sz + 8;
		}
		int rightW = it.right.empty() ? 0 : fonts::body.width(it.right) + 10;
		if (it.bars >= -1) {
			wifiBars(s, r.x + r.w - 22, r.y + (r.h - 11) / 2, it.bars, p.text);
			rightW = 26;
		}
		uint16_t tc = it.enabled ? p.text : p.textDim;
		if (it.subtitle.empty()) {
			fonts::bold.drawEllipsized(s, tx, r.y + (r.h - fonts::bold.lineHeight()) / 2, it.title, r.x + r.w - tx - rightW - 6, tc);
		} else {
			int ty = r.y + (r.h - fonts::bold.lineHeight() - fonts::body.lineHeight() + 2) / 2;
			fonts::bold.drawEllipsized(s, tx, ty, it.title, r.x + r.w - tx - rightW - 6, tc);
			fonts::body.drawEllipsized(s, tx, ty + fonts::bold.lineHeight() - 2, it.subtitle, r.x + r.w - tx - rightW - 6, p.textDim);
		}
		if (!it.right.empty())
			fonts::body.drawRight(s, r.x + r.w - 8, r.y + (r.h - fonts::body.lineHeight()) / 2, it.right, p.textDim);
		if (sel) {
			s.resetClip();
			s.setClip(area.x, area.y, area.w, area.h);
			gfx::strokeRoundRect(s, r.x - 2, r.y - 2, r.w + 4, r.h + 4, 8, 2, p.accent);
		}
	}
	// Scroll indicator
	int total = (int)items.size() * itemHeight;
	if (total > area.h) {
		int barH = area.h * area.h / total;
		if (barH < 12)
			barH = 12;
		int barY = area.y + (area.h - barH) * _scroll / (total - area.h);
		gfx::fillRoundRect(s, area.x + area.w - 5, barY, 3, barH, 1, p.textDim, 160);
	}
	s.resetClip();
	if (items.empty() && showEmptyText)
		fonts::body.drawCentered(s, area.x + area.w / 2, area.y + area.h / 2 - 8,
		                         emptyText.empty() ? std::string(TR("Nothing here yet", "Nada aqui ainda")) : emptyText, p.textDim);
}

} // namespace ui

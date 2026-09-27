// UI toolkit of TWiLight Hub: a flat, rounded look inspired by the current
// Nintendo system menus, with a dark and a light theme.
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "font.h"
#include "gfx.h"
#include "icons.h"

namespace ui {

struct Palette {
	uint16_t bg;        // screen background
	uint16_t bgTop;     // top of the background gradient
	uint16_t panel;     // cards, list items
	uint16_t panelAlt;  // pressed / secondary panels
	uint16_t text;
	uint16_t textDim;
	uint16_t accent;     // selection frame, links
	uint16_t accentText; // text drawn on accent
	uint16_t divider;
	uint16_t danger;
	uint16_t success;
	uint16_t warning;
	bool dark;
};

const Palette &pal();
void applyTheme(bool dark);

struct Rect {
	int x, y, w, h;
	bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
	Rect inset(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};

// Icons ---------------------------------------------------------------------
bool initIcons();
int iconSize(IconSize size);
void icon(Surface &s, IconId id, IconSize size, int x, int y, uint16_t color, int alpha = 255);
// App tile: gradient rounded square with a white glyph
void appTile(Surface &s, const Rect &r, IconId id, uint16_t color, bool large);

// Chrome ------------------------------------------------------------------
void background(Surface &s);
void statusBar(Surface &top);
// Title of an app on the top screen (below the status bar). Returns the y
// coordinate where content can start.
int titleBar(Surface &top, IconId id, uint16_t color, const std::string &title, const std::string &subtitle = "");

struct Hint {
	std::string button; // "A", "B", "X", "Y", "L", "R", "+", "-"
	std::string label;
	uint32_t key;
};
void footer(Surface &bottom, const std::vector<Hint> &hints, const std::string &left = "");
// Key of a footer hint tapped with the stylus this frame (0 if none)
uint32_t footerTapped();

// Widgets -------------------------------------------------------------------
void card(Surface &s, const Rect &r, int radius = 10);
void button(Surface &s, const Rect &r, const std::string &label, bool focused, IconId id = ICON_COUNT, bool primary = false);
void roundButton(Surface &s, int cx, int cy, int radius, IconId id, bool focused, uint16_t glyphColor, const std::string &text = "");
// Animated selection frame (pulsing accent colour)
void cursorFrame(Surface &s, const Rect &r, int radius);
void spinner(Surface &s, int cx, int cy, int radius);
void progressBar(Surface &s, const Rect &r, int percent);
void toggle(Surface &s, int x, int y, bool on);
void wifiBars(Surface &s, int x, int y, int bars, uint16_t color); // bars: -1 off, 0..3
void battery(Surface &s, int x, int y, int percent, bool charging, uint16_t color);

// Touch helpers -------------------------------------------------------------
// True on the frame the stylus is released inside r, if it was pressed in r
bool tapped(const Rect &r);
// True on the frame the stylus touches r
bool pressedIn(const Rect &r);

// Transient message shown at the bottom of the top screen
void toast(const std::string &text, int frames = 150);
void drawToast(Surface &top);

// Modal dialogs (blocking, they keep the current screens dimmed behind) ---
void snapshot();
void restoreSnapshot();
void message(const std::string &title, const std::string &text);
bool confirm(const std::string &title, const std::string &text, const std::string &yes, const std::string &no);
int choose(const std::string &title, const std::vector<std::string> &options, int current);
bool keyboard(const std::string &title, std::string &text, int maxLen, bool secret = false, const std::string &placeholder = "");
// Draws one frame of a "please wait" screen. percent < 0: spinner only.
void busy(const std::string &title, const std::string &text, int percent, bool cancellable);

// Scrollable list ------------------------------------------------------------
struct ListItem {
	std::string title;
	std::string subtitle;
	std::string right;
	IconId icon = ICON_COUNT;
	uint16_t iconColor = 0;
	bool enabled = true;
	int bars = -2; // >= -1: draw a WiFi signal indicator on the right
};

class ListView {
public:
	Rect area{0, 0, 256, 168};
	int itemHeight = 34;
	int selected = 0;
	std::vector<ListItem> items;
	bool focused = true;
	std::string emptyText; // shown when there are no items (default text if empty)
	bool showEmptyText = true;

	// Handles input. Returns the index of the item activated with A or a tap,
	// -1 otherwise.
	int update();
	void draw(Surface &s);
	void ensureVisible();
	int scroll() const { return _scroll; }

private:
	int _scroll = 0;
	bool _dragging = false;
	int _dragStartY = 0, _dragStartScroll = 0, _pressIndex = -1;
	bool _pressed = false;
};

} // namespace ui

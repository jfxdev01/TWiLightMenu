// Image decoding (PNG, JPEG, GIF, BMP through stb_image) and a viewer
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "gfx.h"

struct Image {
	std::vector<uint16_t> px; // BGR555 | 0x8000
	int w = 0, h = 0;
	bool ok() const { return w > 0 && h > 0; }
};

namespace image {

// Images bigger than maxDim on any side are scaled down while loading
bool decode(const uint8_t *data, size_t size, Image &out, int maxDim = 1024);
bool load(const std::string &path, Image &out, int maxDim = 1024);
// High quality (box filter) resize
void resize(const Image &src, Image &dst, int w, int h);
// Scales an image to fit in a w x h box keeping the aspect ratio
void fit(const Image &src, Image &dst, int w, int h);
// Draws an image centred in a rectangle (scaled to fit, cached by the caller)
void drawFitted(Surface &s, const Image &fitted, int x, int y, int w, int h);

// Full screen viewer: fitted image on the top screen, 1:1 detail on the
// bottom screen that can be dragged around. Returns when B is pressed.
// extraHints / onKey allow the caller to add actions (e.g. delete, next).
typedef bool (*ViewerKeyFn)(void *user, uint32_t keys); // return true to close
void view(const Image &img, const std::string &title, const std::string &info, ViewerKeyFn onKey = nullptr, void *user = nullptr,
          const std::string &extraButton = "", const std::string &extraLabel = "", uint32_t extraKey = 0);

} // namespace image

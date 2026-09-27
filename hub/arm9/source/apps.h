// Apps of TWiLight Hub. Each one runs its own loop and returns to the home
// screen when closed.
#pragma once

#include <stdint.h>
#include <string>

#include "icons.h"

namespace apps {

void camera();
void album();
void browser(const std::string &url = "");
void wifi();
void weather();
void clock();
void boxart();
void system();

struct AppInfo {
	void (*run)();
	IconId icon;
	uint16_t color;
	const char *nameEn, *namePt;
	const char *descEn, *descPt;
	bool needsDSi;
};

const AppInfo *list(int &count);

} // namespace apps

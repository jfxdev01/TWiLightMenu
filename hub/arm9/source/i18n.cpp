#include "i18n.h"

#include "config.h"
#include "platform.h"

namespace i18n {

static bool portuguese = false;

void init() {
	std::string forced = config::ini().get("HUB", "LANGUAGE", "auto");
	if (forced == "pt") {
		portuguese = true;
		return;
	}
	if (forced == "en") {
		portuguese = false;
		return;
	}
	// TWiLight Menu++ languages: 10 = Portuguese, 25 = Portuguese (Brazil)
	int lang = config::twlLanguage();
	portuguese = (lang == 10 || lang == 25);
}

bool isPt() { return portuguese; }

void setPt(bool pt) {
	portuguese = pt;
	config::ini().set("HUB", "LANGUAGE", pt ? "pt" : "en");
}

} // namespace i18n

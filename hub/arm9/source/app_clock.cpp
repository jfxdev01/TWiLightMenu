// Clock: sets the console's real time clock from an internet time server
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "apps.h"
#include "config.h"
#include "i18n.h"
#include "net.h"
#include "platform.h"
#include "ui.h"

namespace apps {

namespace {

const uint16_t kColor = HEXCOLOR(0x7D5AD2);

std::string offsetText(int minutes) {
	char buf[24];
	int a = abs(minutes);
	snprintf(buf, sizeof(buf), "UTC%c%d:%02d", minutes < 0 ? '-' : '+', a / 60, a % 60);
	return buf;
}

const char *monthName(int m) {
	static const char *en[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
	static const char *pt[12] = {"janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho", "agosto", "setembro", "outubro", "novembro", "dezembro"};
	return i18n::isPt() ? pt[m % 12] : en[m % 12];
}

const char *dayName(int d) {
	static const char *en[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
	static const char *pt[7] = {"domingo", "segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira", "sábado"};
	return i18n::isPt() ? pt[d % 7] : en[d % 7];
}

bool syncNow(int offsetMinutes, std::string &result) {
	if (!net::ensureConnected()) {
		result = TR("No internet connection", "Sem conexão com a internet");
		return false;
	}
	std::string server = config::ini().get("CLOCK", "SERVER", "pool.ntp.org");
	ui::busy(TR("Clock", "Relógio"), std::string(TR("Asking ", "Consultando ")) + server, -1, false);
	time_t utc;
	std::string err;
	if (!net::ntpTime(server, utc, err)) {
		result = err;
		return false;
	}
	time_t local = utc + offsetMinutes * 60;
	struct tm t;
	gmtime_r(&local, &t);
	time_t before = platform::now();
	if (!platform::setClock(t)) {
		result = TR("The clock couldn't be written", "Não foi possível gravar o relógio");
		return false;
	}
	long diff = (long)(local - before);
	char buf[96];
	if (labs(diff) < 2)
		snprintf(buf, sizeof(buf), "%s", TR("The clock was already right!", "O relógio já estava certo!"));
	else
		snprintf(buf, sizeof(buf), TR("Adjusted by %+ld s", "Ajustado em %+ld s"), diff);
	result = buf;
	return true;
}

} // namespace

void clock() {
	int offset = config::ini().getInt("CLOCK", "UTC_OFFSET_MIN", i18n::isPt() ? -180 : 0);
	std::string lastResult;
	int focus = 0; // 0: sync button, 1: offset

	while (true) {
		platform::scanInput();
		const ui::Palette &p = ui::pal();
		Surface &top = platform::top();
		Surface &bottom = platform::bottom();

		time_t now = platform::now();
		struct tm *t = localtime(&now);
		ui::background(top);
		ui::statusBar(top);
		char hm[16], sec[8], date[96];
		snprintf(hm, sizeof(hm), "%02d:%02d", t->tm_hour, t->tm_min);
		snprintf(sec, sizeof(sec), ":%02d", t->tm_sec);
		int w = fonts::huge.width(hm) + fonts::title.width(sec);
		int x = 128 - w / 2;
		x = fonts::huge.draw(top, x, 58, hm, p.text);
		fonts::title.draw(top, x, 76, sec, p.textDim);
		if (i18n::isPt())
			snprintf(date, sizeof(date), "%s, %d de %s de %d", dayName(t->tm_wday), t->tm_mday, monthName(t->tm_mon), t->tm_year + 1900);
		else
			snprintf(date, sizeof(date), "%s, %s %d, %d", dayName(t->tm_wday), monthName(t->tm_mon), t->tm_mday, t->tm_year + 1900);
		fonts::body.drawCentered(top, 128, 104, date, p.textDim);
		ui::card(top, {10, 124, 236, 60}, 10);
		fonts::body.drawWrapped(top, 20, 130, lastResult.empty() ? std::string(TR("The DSi clock drifts over time. Sync it with an internet time server.",
		                                                                         "O relógio do DSi atrasa com o tempo. Sincronize com um servidor de hora da internet."))
		                                                         : lastResult,
		                        216, lastResult.empty() ? p.textDim : p.text, 3);
		ui::drawToast(top);

		ui::background(bottom);
		ui::Rect sync{28, 24, 200, 40};
		ui::button(bottom, sync, TR("Sync now", "Sincronizar agora"), focus == 0, ICON_REFRESH, true);
		fonts::body.drawCentered(bottom, 128, 80, TR("Time zone", "Fuso horário"), p.textDim);
		ui::Rect minus{28, 98, 40, 36}, plus{188, 98, 40, 36}, mid{72, 98, 112, 36};
		ui::button(bottom, minus, "-", false);
		ui::button(bottom, plus, "+", false);
		gfx::fillRoundRect(bottom, mid.x, mid.y, mid.w, mid.h, 8, p.panel);
		fonts::title.drawCentered(bottom, 128, mid.y + 9, offsetText(offset), p.text);
		if (focus == 1)
			ui::cursorFrame(bottom, mid, 8);
		fonts::body.drawCentered(bottom, 128, 142, TR("Brasília time is UTC-3:00", "Horário de Brasília é UTC-3:00"), p.textDim);
		ui::footer(bottom, {{"B", TR("Back", "Voltar"), KEY_B}, {"A", TR("Sync", "Sincronizar"), KEY_A}});

		uint32_t kd = platform::keysDown() | ui::footerTapped();
		uint32_t kr = platform::keysRepeat();
		int oldOffset = offset;
		if (kd & (KEY_UP | KEY_DOWN)) {
			focus ^= 1;
			platform::playSfx(platform::SFX_MOVE);
		}
		if (focus == 1 && (kr & KEY_LEFT))
			offset -= 30;
		if (focus == 1 && (kr & KEY_RIGHT))
			offset += 30;
		if (ui::tapped(minus))
			offset -= 30;
		if (ui::tapped(plus))
			offset += 30;
		if (offset < -12 * 60)
			offset = -12 * 60;
		if (offset > 14 * 60)
			offset = 14 * 60;
		if (offset != oldOffset) {
			platform::playSfx(platform::SFX_MOVE);
			config::ini().setInt("CLOCK", "UTC_OFFSET_MIN", offset);
			config::save();
		}
		if (((kd & KEY_A) && focus == 0) || ui::tapped(sync)) {
			bool ok = syncNow(offset, lastResult);
			platform::playSfx(ok ? platform::SFX_SELECT : platform::SFX_ERROR);
			if (ok)
				ui::toast(TR("Clock synced", "Relógio sincronizado"));
		}
		if (kd & KEY_B) {
			platform::playSfx(platform::SFX_BACK);
			return;
		}
		platform::present();
	}
}

} // namespace apps

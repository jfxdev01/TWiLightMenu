// Weather: current conditions and a 3 day forecast from wttr.in
#include <stdio.h>
#include <stdlib.h>

#include "apps.h"
#include "config.h"
#include "i18n.h"
#include "json.h"
#include "net.h"
#include "platform.h"
#include "ui.h"

namespace apps {

namespace {

const uint16_t kColor = HEXCOLOR(0xFFA01E);

struct Condition {
	IconId icon;
	uint16_t color;
};

Condition conditionFor(int code) {
	switch (code) {
		case 113:
			return {ICON_SUN, HEXCOLOR(0xFFB400)};
		case 116:
			return {ICON_WEATHER, HEXCOLOR(0xFFA01E)};
		case 119:
		case 122:
			return {ICON_CLOUD, HEXCOLOR(0x8C9BAA)};
		case 143:
		case 248:
		case 260:
			return {ICON_FOG, HEXCOLOR(0x9AA5B0)};
		case 200:
		case 386:
		case 389:
		case 392:
		case 395:
			return {ICON_STORM, HEXCOLOR(0x6E5AB4)};
		case 179:
		case 182:
		case 185:
		case 227:
		case 230:
		case 311:
		case 314:
		case 317:
		case 320:
		case 323:
		case 326:
		case 329:
		case 332:
		case 335:
		case 338:
		case 350:
		case 362:
		case 365:
		case 368:
		case 371:
		case 374:
		case 377:
			return {ICON_SNOW, HEXCOLOR(0x78B4E6)};
		default:
			return {ICON_RAIN, HEXCOLOR(0x3C8CDC)};
	}
}

struct Day {
	std::string date;
	int minC, maxC, code, rain;
	std::string desc;
};

struct Weather {
	bool ok = false;
	std::string place;
	std::string desc;
	int tempC = 0, feelsC = 0, humidity = 0, wind = 0, code = 113;
	std::string windDir;
	std::vector<Day> days;
};

std::string descOf(const Json &j) {
	if (i18n::isPt()) {
		std::string pt = j["lang_pt"][0]["value"].str();
		if (!pt.empty())
			return pt;
	}
	return j["weatherDesc"][0]["value"].str();
}

bool fetchWeather(const std::string &city, Weather &w) {
	net::Request req;
	req.url = "https://wttr.in/" + net::urlEncode(city) + "?format=j1&lang=" + (i18n::isPt() ? "pt" : "en");
	req.maxBytes = 256 * 1024;
	net::Response res;
	if (!net::fetchWithUi(TR("Weather", "Clima"), req, res)) {
		ui::message(TR("Weather", "Clima"), res.error);
		return false;
	}
	Json j;
	if (res.status != 200 || !Json::parse(res.body, j) || j["current_condition"].size() == 0) {
		ui::message(TR("Weather", "Clima"), TR("The weather service didn't recognise this place. Try \"City, Country\".",
		                                       "O serviço de clima não reconheceu este lugar. Tente \"Cidade, País\"."));
		return false;
	}
	const Json &cur = j["current_condition"][0];
	w.tempC = cur["temp_C"].num();
	w.feelsC = cur["FeelsLikeC"].num();
	w.humidity = cur["humidity"].num();
	w.wind = cur["windspeedKmph"].num();
	w.windDir = cur["winddir16Point"].str();
	w.code = cur["weatherCode"].num(113);
	w.desc = descOf(cur);
	const Json &area = j["nearest_area"][0];
	w.place = area["areaName"][0]["value"].str(city);
	std::string country = area["country"][0]["value"].str();
	if (!country.empty())
		w.place += ", " + country;
	w.days.clear();
	for (size_t i = 0; i < j["weather"].size() && i < 3; i++) {
		const Json &d = j["weather"][i];
		Day day;
		day.date = d["date"].str();
		day.minC = d["mintempC"].num();
		day.maxC = d["maxtempC"].num();
		const Json &noon = d["hourly"][4]; // 12:00
		day.code = noon["weatherCode"].num(113);
		day.desc = descOf(noon);
		day.rain = noon["chanceofrain"].num();
		w.days.push_back(day);
	}
	w.ok = true;
	return true;
}

int toUnit(int c, bool fahrenheit) { return fahrenheit ? c * 9 / 5 + 32 : c; }

std::string weekday(const std::string &date, int index) {
	if (index == 0)
		return TR("Today", "Hoje");
	if (index == 1)
		return TR("Tomorrow", "Amanhã");
	// yyyy-mm-dd -> day of week (Zeller's congruence)
	int y = atoi(date.substr(0, 4).c_str()), m = atoi(date.substr(5, 2).c_str()), d = atoi(date.substr(8, 2).c_str());
	if (m < 3) {
		m += 12;
		y--;
	}
	int h = (d + 13 * (m + 1) / 5 + y + y / 4 - y / 100 + y / 400) % 7; // 0 = Saturday
	static const char *en[7] = {"Saturday", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday"};
	static const char *pt[7] = {"Sábado", "Domingo", "Segunda", "Terça", "Quarta", "Quinta", "Sexta"};
	return i18n::isPt() ? pt[h] : en[h];
}

} // namespace

void weather() {
	std::string city = config::ini().get("WEATHER", "CITY");
	bool fahrenheit = config::ini().getInt("WEATHER", "FAHRENHEIT", 0) != 0;
	Weather w;
	if (city.empty()) {
		if (!ui::keyboard(TR("Your city", "Sua cidade"), city, 60, false, TR("e.g. São Paulo", "ex.: São Paulo")) || city.empty())
			return;
		config::ini().set("WEATHER", "CITY", city);
		config::save();
	}
	if (net::ensureConnected())
		fetchWeather(city, w);

	while (true) {
		platform::scanInput();
		const ui::Palette &p = ui::pal();
		Surface &top = platform::top();
		Surface &bottom = platform::bottom();

		ui::background(top);
		ui::statusBar(top);
		if (w.ok) {
			Condition c = conditionFor(w.code);
			ui::appTile(top, {14, 40, 72, 72}, c.icon, c.color, true);
			char t[16];
			snprintf(t, sizeof(t), "%d°", toUnit(w.tempC, fahrenheit));
			fonts::huge.draw(top, 96, 38, t, p.text);
			fonts::body.draw(top, 98 + fonts::huge.width(t), 46, fahrenheit ? "F" : "C", p.textDim);
			fonts::bold.drawEllipsized(top, 98, 84, w.desc, 150, p.text);
			fonts::body.drawEllipsized(top, 98, 100, w.place, 150, p.textDim);
			ui::card(top, {10, 124, 236, 56}, 10);
			char a[48], b[48], cc[48];
			snprintf(a, sizeof(a), "%d°", toUnit(w.feelsC, fahrenheit));
			snprintf(b, sizeof(b), "%d%%", w.humidity);
			snprintf(cc, sizeof(cc), "%d km/h %s", w.wind, w.windDir.c_str());
			const char *labels[3] = {TR("Feels like", "Sensação"), TR("Humidity", "Umidade"), TR("Wind", "Vento")};
			const char *vals[3] = {a, b, cc};
			for (int i = 0; i < 3; i++) {
				int cx = 10 + 236 * (2 * i + 1) / 6;
				fonts::body.drawCentered(top, cx, 132, labels[i], p.textDim);
				fonts::bold.drawCentered(top, cx, 152, vals[i], p.text);
			}
		} else {
			ui::titleBar(top, ICON_WEATHER, kColor, TR("Weather", "Clima"), city);
			fonts::body.drawWrapped(top, 16, 100, TR("No data. Press A to try again.", "Sem dados. Aperte A para tentar de novo."), 224, p.textDim);
		}
		ui::drawToast(top);

		ui::background(bottom);
		if (w.ok) {
			fonts::bold.draw(bottom, 12, 8, TR("Next days", "Próximos dias"), p.text);
			for (size_t i = 0; i < w.days.size(); i++) {
				const Day &d = w.days[i];
				ui::Rect r{8 + (int)i * 81, 30, 76, 130};
				ui::card(bottom, r, 10);
				Condition c = conditionFor(d.code);
				fonts::bold.drawCentered(bottom, r.x + r.w / 2, r.y + 8, fonts::bold.ellipsize(weekday(d.date, (int)i), r.w - 8), p.text);
				ui::icon(bottom, c.icon, ICONSIZE_40, r.x + (r.w - 40) / 2, r.y + 28, c.color);
				char mm[32];
				snprintf(mm, sizeof(mm), "%d° / %d°", toUnit(d.maxC, fahrenheit), toUnit(d.minC, fahrenheit));
				fonts::bold.drawCentered(bottom, r.x + r.w / 2, r.y + 74, mm, p.text);
				char rain[24];
				snprintf(rain, sizeof(rain), "%s %d%%", TR("Rain", "Chuva"), d.rain);
				fonts::body.drawCentered(bottom, r.x + r.w / 2, r.y + 94, rain, p.textDim);
				std::vector<std::string> lines;
				fonts::body.wrap(d.desc, r.w - 8, lines);
				if (!lines.empty())
					fonts::body.drawCentered(bottom, r.x + r.w / 2, r.y + 110, fonts::body.ellipsize(lines[0], r.w - 8), p.textDim);
			}
		}
		ui::footer(bottom, {{"B", TR("Back", "Voltar"), KEY_B}, {"Y", TR("City", "Cidade"), KEY_Y}, {"X", fahrenheit ? "°C" : "°F", KEY_X}, {"A", TR("Update", "Atualizar"), KEY_A}});

		uint32_t kd = platform::keysDown() | ui::footerTapped();
		if (kd & KEY_A) {
			if (net::ensureConnected())
				fetchWeather(city, w);
		}
		if (kd & KEY_Y) {
			std::string c = city;
			if (ui::keyboard(TR("Your city", "Sua cidade"), c, 60, false, TR("e.g. São Paulo", "ex.: São Paulo")) && !c.empty()) {
				city = c;
				config::ini().set("WEATHER", "CITY", city);
				config::save();
				if (net::ensureConnected())
					fetchWeather(city, w);
			}
		}
		if (kd & KEY_X) {
			fahrenheit = !fahrenheit;
			config::ini().setInt("WEATHER", "FAHRENHEIT", fahrenheit);
			config::save();
		}
		if (kd & KEY_B) {
			platform::playSfx(platform::SFX_BACK);
			return;
		}
		platform::present();
	}
}

} // namespace apps

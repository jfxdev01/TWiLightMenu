#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "platform.h"

static std::string trim(const std::string &s) {
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos)
		return "";
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

bool IniFile::load(const std::string &path) {
	_sections.clear();
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	char line[1024];
	Section *cur = nullptr;
	while (fgets(line, sizeof(line), f)) {
		std::string l = trim(line);
		if (l.empty() || l[0] == ';' || l[0] == '#')
			continue;
		if (l[0] == '[') {
			size_t e = l.find(']');
			std::string name = l.substr(1, e == std::string::npos ? std::string::npos : e - 1);
			cur = find(name);
			if (!cur) {
				_sections.push_back({name, {}});
				cur = &_sections.back();
			}
			continue;
		}
		size_t eq = l.find('=');
		if (eq == std::string::npos)
			continue;
		if (!cur) {
			_sections.push_back({"", {}});
			cur = &_sections.back();
		}
		cur->values.emplace_back(trim(l.substr(0, eq)), trim(l.substr(eq + 1)));
	}
	fclose(f);
	return true;
}

bool IniFile::save(const std::string &path) const {
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return false;
	for (const Section &s : _sections) {
		if (!s.name.empty())
			fprintf(f, "[%s]\n", s.name.c_str());
		for (const auto &kv : s.values)
			fprintf(f, "%s = %s\n", kv.first.c_str(), kv.second.c_str());
		fprintf(f, "\n");
	}
	fclose(f);
	return true;
}

IniFile::Section *IniFile::find(const std::string &name) {
	for (Section &s : _sections)
		if (s.name == name)
			return &s;
	return nullptr;
}

const IniFile::Section *IniFile::find(const std::string &name) const {
	for (const Section &s : _sections)
		if (s.name == name)
			return &s;
	return nullptr;
}

std::string IniFile::get(const std::string &section, const std::string &key, const std::string &def) const {
	const Section *s = find(section);
	if (s)
		for (const auto &kv : s->values)
			if (kv.first == key)
				return kv.second;
	return def;
}

int IniFile::getInt(const std::string &section, const std::string &key, int def) const {
	std::string v = get(section, key);
	if (v.empty())
		return def;
	return (int)strtol(v.c_str(), nullptr, 0);
}

void IniFile::set(const std::string &section, const std::string &key, const std::string &value) {
	Section *s = find(section);
	if (!s) {
		_sections.push_back({section, {}});
		s = &_sections.back();
	}
	for (auto &kv : s->values) {
		if (kv.first == key) {
			kv.second = value;
			return;
		}
	}
	s->values.emplace_back(key, value);
}

void IniFile::setInt(const std::string &section, const std::string &key, int value) {
	set(section, key, std::to_string(value));
}

void IniFile::remove(const std::string &section, const std::string &key) {
	Section *s = find(section);
	if (!s)
		return;
	for (size_t i = 0; i < s->values.size(); i++) {
		if (s->values[i].first == key) {
			s->values.erase(s->values.begin() + i);
			return;
		}
	}
}

std::vector<std::pair<std::string, std::string>> IniFile::entries(const std::string &section) const {
	const Section *s = find(section);
	return s ? s->values : std::vector<std::pair<std::string, std::string>>();
}

void IniFile::clearSection(const std::string &section) {
	Section *s = find(section);
	if (s)
		s->values.clear();
}

namespace config {

static IniFile hubIni;
static IniFile twlIni;

std::string hubDir() { return platform::twlDir() + "/hub"; }

IniFile &ini() { return hubIni; }

void load() {
	hubIni.load(hubDir() + "/hub.ini");
	twlIni.load(platform::twlDir() + "/settings.ini");
}

void save() {
	mkdir(hubDir().c_str(), 0777);
	hubIni.save(hubDir() + "/hub.ini");
}

bool darkTheme() { return hubIni.getInt("HUB", "DARK_THEME", 1) != 0; }
void setDarkTheme(bool dark) { hubIni.setInt("HUB", "DARK_THEME", dark); }
bool soundEffects() { return hubIni.getInt("HUB", "SOUND_EFFECTS", 1) != 0; }
void setSoundEffects(bool on) { hubIni.setInt("HUB", "SOUND_EFFECTS", on); }
bool dsiWifi() { return hubIni.getInt("WIFI", "DSI_MODE", 1) != 0; }
void setDsiWifi(bool on) { hubIni.setInt("WIFI", "DSI_MODE", on); }

int twlLanguage() { return twlIni.getInt("SRLOADER", "LANGUAGE", -1); }
int twlTheme() { return twlIni.getInt("SRLOADER", "THEME", 0); }
int consoleModel() { return twlIni.getInt("SRLOADER", "CONSOLE_MODEL", 0); }

std::vector<std::string> romFolders() {
	std::vector<std::string> out;
	std::string a = twlIni.get("SRLOADER", "ROM_FOLDER");
	std::string b = twlIni.get("SRLOADER", "SECONDARY_ROM_FOLDER");
	if (!a.empty())
		out.push_back(a);
	if (!b.empty() && b != a)
		out.push_back(b);
	if (out.empty()) {
		out.push_back(platform::root() + "/roms");
		out.push_back(platform::root() + "/");
	}
	return out;
}

} // namespace config

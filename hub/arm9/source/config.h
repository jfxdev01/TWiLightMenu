// Settings of TWiLight Hub (<TWiLight dir>/hub/hub.ini) and the values it
// reads from TWiLight Menu++'s own settings.ini.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

class IniFile {
public:
	bool load(const std::string &path);
	bool save(const std::string &path) const;

	std::string get(const std::string &section, const std::string &key, const std::string &def = "") const;
	int getInt(const std::string &section, const std::string &key, int def = 0) const;
	void set(const std::string &section, const std::string &key, const std::string &value);
	void setInt(const std::string &section, const std::string &key, int value);
	void remove(const std::string &section, const std::string &key);
	std::vector<std::pair<std::string, std::string>> entries(const std::string &section) const;
	void clearSection(const std::string &section);

private:
	struct Section {
		std::string name;
		std::vector<std::pair<std::string, std::string>> values;
	};
	Section *find(const std::string &name);
	const Section *find(const std::string &name) const;
	std::vector<Section> _sections;
};

namespace config {

void load();
void save();
IniFile &ini();

std::string hubDir(); // <TWiLight dir>/hub

// Hub settings
bool darkTheme();
void setDarkTheme(bool dark);
bool soundEffects();
void setSoundEffects(bool on);
bool dsiWifi(); // true: DSi mode WiFi (WPA2), false: DS mode (WEP/open)
void setDsiWifi(bool on);

// Values from TWiLight Menu++'s settings.ini
int twlLanguage();   // TWLSettings::TLanguage (-1 = firmware)
int twlTheme();      // TWLSettings::TTheme
int consoleModel();  // 0 DSi, 1 devkit, 2 3DS, 3 New 3DS
std::vector<std::string> romFolders();

} // namespace config

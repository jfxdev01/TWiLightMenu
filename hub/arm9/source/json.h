// Small JSON parser (numbers are kept as text, the ARM9 has no FPU)
#pragma once

#include <string>
#include <utility>
#include <vector>

struct Json {
	enum Type { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT } type = NUL;
	std::string text; // STRING value, NUMBER/BOOL as written
	std::vector<Json> items;
	std::vector<std::pair<std::string, Json>> members;

	const Json &operator[](const char *key) const;
	const Json &operator[](size_t index) const;
	const Json &operator[](int index) const { return (*this)[(size_t)index]; }
	size_t size() const { return type == ARRAY ? items.size() : members.size(); }
	std::string str(const std::string &def = "") const { return type == NUL ? def : text; }
	int num(int def = 0) const;

	static bool parse(const std::string &src, Json &out);
};

#include "json.h"

#include <ctype.h>
#include <stdlib.h>

#include "font.h"

static const Json kNull;

const Json &Json::operator[](const char *key) const {
	if (type != OBJECT)
		return kNull;
	for (const auto &m : members)
		if (m.first == key)
			return m.second;
	return kNull;
}

const Json &Json::operator[](size_t index) const {
	if (type != ARRAY || index >= items.size())
		return kNull;
	return items[index];
}

int Json::num(int def) const {
	if (type == NUL || text.empty())
		return def;
	return atoi(text.c_str());
}

namespace {

struct Parser {
	const std::string &s;
	size_t i = 0;
	int depth = 0;

	explicit Parser(const std::string &src) : s(src) {}

	void ws() {
		while (i < s.size() && isspace((unsigned char)s[i]))
			i++;
	}

	bool str(std::string &out) {
		if (s[i] != '"')
			return false;
		i++;
		while (i < s.size() && s[i] != '"') {
			char c = s[i++];
			if (c != '\\') {
				out += c;
				continue;
			}
			if (i >= s.size())
				return false;
			char e = s[i++];
			switch (e) {
				case 'n':
					out += '\n';
					break;
				case 't':
					out += '\t';
					break;
				case 'r':
				case 'b':
				case 'f':
					break;
				case 'u': {
					if (i + 4 > s.size())
						return false;
					uint32_t cp = strtoul(s.substr(i, 4).c_str(), nullptr, 16);
					i += 4;
					// Surrogate pair
					if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
						uint32_t lo = strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
						if (lo >= 0xDC00 && lo < 0xE000) {
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
							i += 6;
						}
					}
					utf8Append(out, cp);
					break;
				}
				default:
					out += e;
			}
		}
		if (i >= s.size())
			return false;
		i++;
		return true;
	}

	bool value(Json &out) {
		if (++depth > 64)
			return false;
		ws();
		if (i >= s.size())
			return false;
		char c = s[i];
		bool ok = true;
		if (c == '{') {
			out.type = Json::OBJECT;
			i++;
			ws();
			if (i < s.size() && s[i] == '}') {
				i++;
			} else {
				while (true) {
					ws();
					std::string key;
					if (i >= s.size() || !str(key)) {
						ok = false;
						break;
					}
					ws();
					if (i >= s.size() || s[i] != ':') {
						ok = false;
						break;
					}
					i++;
					out.members.emplace_back(key, Json());
					if (!value(out.members.back().second)) {
						ok = false;
						break;
					}
					ws();
					if (i < s.size() && s[i] == ',') {
						i++;
						continue;
					}
					if (i < s.size() && s[i] == '}') {
						i++;
						break;
					}
					ok = false;
					break;
				}
			}
		} else if (c == '[') {
			out.type = Json::ARRAY;
			i++;
			ws();
			if (i < s.size() && s[i] == ']') {
				i++;
			} else {
				while (true) {
					out.items.emplace_back();
					if (!value(out.items.back())) {
						ok = false;
						break;
					}
					ws();
					if (i < s.size() && s[i] == ',') {
						i++;
						continue;
					}
					if (i < s.size() && s[i] == ']') {
						i++;
						break;
					}
					ok = false;
					break;
				}
			}
		} else if (c == '"') {
			out.type = Json::STRING;
			ok = str(out.text);
		} else if (c == 't' || c == 'f') {
			out.type = Json::BOOL;
			out.text = c == 't' ? "true" : "false";
			i += c == 't' ? 4 : 5;
		} else if (c == 'n') {
			out.type = Json::NUL;
			i += 4;
		} else {
			out.type = Json::NUMBER;
			size_t st = i;
			while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
				i++;
			out.text = s.substr(st, i - st);
			ok = i > st;
		}
		depth--;
		return ok;
	}
};

} // namespace

bool Json::parse(const std::string &src, Json &out) {
	out = Json();
	Parser p(src);
	return p.value(out);
}

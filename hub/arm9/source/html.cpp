#include "html.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "i18n.h"
#include "net.h"

namespace html {

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

static std::string lower(std::string s) {
	for (char &c : s)
		c = (char)tolower((unsigned char)c);
	return s;
}

static bool validUtf8(const std::string &s) {
	size_t i = 0, n = s.size();
	while (i < n) {
		unsigned char c = s[i];
		int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
		if (extra < 0)
			return false;
		for (int k = 1; k <= extra; k++)
			if (i + k >= n || (((unsigned char)s[i + k]) & 0xC0) != 0x80)
				return false;
		i += extra + 1;
	}
	return true;
}

// Windows-1252 characters in the 0x80-0x9F range
static const uint16_t cp1252[32] = {
		0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
		0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178,
};

std::string latin1ToUtf8(const std::string &s) {
	std::string out;
	out.reserve(s.size() + s.size() / 8);
	for (unsigned char c : s) {
		uint32_t cp = c;
		if (c >= 0x80 && c < 0xA0)
			cp = cp1252[c - 0x80];
		utf8Append(out, cp);
	}
	return out;
}

struct Entity {
	const char *name;
	uint16_t cp;
};

static const Entity kEntities[] = {
	{"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
	{"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"hellip", 0x2026}, {"mdash", 0x2014},
	{"ndash", 0x2013}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
	{"sbquo", 0x201A}, {"bdquo", 0x201E}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"bull", 0x2022},
	{"middot", 0xB7}, {"deg", 0xB0}, {"euro", 0x20AC}, {"pound", 0xA3}, {"yen", 0xA5},
	{"cent", 0xA2}, {"sect", 0xA7}, {"para", 0xB6}, {"times", 0xD7}, {"divide", 0xF7},
	{"plusmn", 0xB1}, {"frac12", 0xBD}, {"frac14", 0xBC}, {"frac34", 0xBE}, {"iexcl", 0xA1},
	{"iquest", 0xBF}, {"szlig", 0xDF}, {"ordm", 0xBA}, {"ordf", 0xAA}, {"sup1", 0xB9},
	{"sup2", 0xB2}, {"sup3", 0xB3}, {"micro", 0xB5}, {"shy", 0xAD}, {"larr", 0x2190},
	{"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193}, {"ensp", 0x2002}, {"emsp", 0x2003},
	{"thinsp", 0x2009}, {"zwnj", 0x200C}, {"zwj", 0x200D}, {"lrm", 0x200E}, {"rlm", 0x200F},
	{"prime", 0x2032}, {"Prime", 0x2033}, {"minus", 0x2212}, {"acute", 0xB4}, {"cedil", 0xB8},
	{"uml", 0xA8}, {"macr", 0xAF}, {"not", 0xAC}, {"brvbar", 0xA6}, {"curren", 0xA4},
	{"AElig", 0xC6}, {"aelig", 0xE6}, {"ETH", 0xD0}, {"eth", 0xF0}, {"THORN", 0xDE},
	{"thorn", 0xFE}, {"Oslash", 0xD8}, {"oslash", 0xF8}, {"OElig", 0x152}, {"oelig", 0x153},
	{"Scaron", 0x160}, {"scaron", 0x161}, {"Yuml", 0x178}, {"check", 0x2713}, {"star", 0x2606},
};

// Accented Latin letters: base letter + accent name (aacute, Ccedil, ...)
static uint32_t accented(const std::string &name) {
	if (name.size() < 4)
		return 0;
	char base = name[0];
	std::string acc = name.substr(1);
	static const char *kUpper = "AEIOUY";
	static const struct {
		const char *acc;
		const char *letters;
		uint16_t upper[6], lower[6];
	} kTable[] = {
		{"grave", "AEIOU", {0xC0, 0xC8, 0xCC, 0xD2, 0xD9, 0}, {0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0}},
		{"acute", "AEIOUY", {0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xDD}, {0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xFD}},
		{"circ", "AEIOU", {0xC2, 0xCA, 0xCE, 0xD4, 0xDB, 0}, {0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0}},
		{"tilde", "AO", {0xC3, 0xD5, 0, 0, 0, 0}, {0xE3, 0xF5, 0, 0, 0, 0}},
		{"uml", "AEIOUY", {0xC4, 0xCB, 0xCF, 0xD6, 0xDC, 0x178}, {0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xFF}},
		{"ring", "A", {0xC5, 0, 0, 0, 0, 0}, {0xE5, 0, 0, 0, 0, 0}},
	};
	(void)kUpper;
	if (acc == "cedil")
		return base == 'C' ? 0xC7 : base == 'c' ? 0xE7 : 0;
	if (name == "Ntilde")
		return 0xD1;
	if (name == "ntilde")
		return 0xF1;
	for (auto &t : kTable) {
		if (acc != t.acc)
			continue;
		const char *pos = strchr(t.letters, toupper((unsigned char)base));
		if (!pos)
			return 0;
		int i = pos - t.letters;
		return isupper((unsigned char)base) ? t.upper[i] : t.lower[i];
	}
	return 0;
}

std::string decodeEntities(const std::string &s) {
	if (s.find('&') == std::string::npos)
		return s;
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] != '&') {
			out += s[i];
			continue;
		}
		size_t semi = s.find(';', i);
		if (semi == std::string::npos || semi - i > 10) {
			out += '&';
			continue;
		}
		std::string name = s.substr(i + 1, semi - i - 1);
		uint32_t cp = 0;
		if (!name.empty() && name[0] == '#') {
			if (name.size() > 1 && (name[1] == 'x' || name[1] == 'X'))
				cp = strtoul(name.c_str() + 2, nullptr, 16);
			else
				cp = strtoul(name.c_str() + 1, nullptr, 10);
			if (cp >= 0x80 && cp < 0xA0) // cp1252 numeric references
				cp = cp1252[cp - 0x80];
		} else {
			for (const Entity &e : kEntities) {
				if (name == e.name) {
					cp = e.cp;
					break;
				}
			}
			if (!cp)
				cp = accented(name);
		}
		if (cp == 0 || cp > 0x10FFFF) {
			out += '&';
			continue;
		}
		utf8Append(out, cp);
		i = semi;
	}
	return out;
}

// ---------------------------------------------------------------------------
// URLs
// ---------------------------------------------------------------------------

static std::string trim(const std::string &s) {
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos)
		return "";
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

static std::string normalisePath(const std::string &path) {
	std::vector<std::string> parts;
	size_t i = 0;
	bool trailing = !path.empty() && path.back() == '/';
	while (i <= path.size()) {
		size_t j = path.find('/', i);
		if (j == std::string::npos)
			j = path.size();
		std::string part = path.substr(i, j - i);
		if (part == "..") {
			if (!parts.empty())
				parts.pop_back();
		} else if (!part.empty() && part != ".") {
			parts.push_back(part);
		}
		i = j + 1;
	}
	std::string out = "/";
	for (size_t k = 0; k < parts.size(); k++) {
		out += parts[k];
		if (k + 1 < parts.size())
			out += '/';
	}
	if (trailing && out.size() > 1)
		out += '/';
	return out;
}

std::string resolveUrl(const std::string &baseIn, const std::string &hrefIn) {
	std::string href = trim(hrefIn);
	std::string base = baseIn;
	size_t colon = href.find(':');
	size_t slash = href.find('/');
	if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
		bool scheme = true;
		for (size_t i = 0; i < colon; i++)
			if (!isalnum((unsigned char)href[i]) && href[i] != '+' && href[i] != '-' && href[i] != '.')
				scheme = false;
		if (scheme && colon > 0)
			return href;
	}
	size_t schemeEnd = base.find("://");
	if (schemeEnd == std::string::npos)
		return href;
	std::string scheme = base.substr(0, schemeEnd);
	size_t hostStart = schemeEnd + 3;
	size_t pathStart = base.find_first_of("/?#", hostStart);
	std::string host = base.substr(hostStart, pathStart == std::string::npos ? std::string::npos : pathStart - hostStart);
	std::string path = pathStart == std::string::npos ? "/" : base.substr(pathStart);
	size_t q = path.find_first_of("?#");
	std::string pathOnly = q == std::string::npos ? path : path.substr(0, q);
	if (pathOnly.empty())
		pathOnly = "/";

	if (href.compare(0, 2, "//") == 0)
		return scheme + ":" + href;
	if (href.empty())
		return base;
	if (href[0] == '#')
		return scheme + "://" + host + path.substr(0, path.find('#')) + href;
	if (href[0] == '?')
		return scheme + "://" + host + pathOnly + href;
	if (href[0] == '/') {
		size_t hq = href.find_first_of("?#");
		return scheme + "://" + host + normalisePath(href.substr(0, hq)) + (hq == std::string::npos ? "" : href.substr(hq));
	}
	std::string dir = pathOnly.substr(0, pathOnly.rfind('/') + 1);
	size_t hq = href.find_first_of("?#");
	std::string rel = href.substr(0, hq);
	return scheme + "://" + host + normalisePath(dir + rel) + (hq == std::string::npos ? "" : href.substr(hq));
}

std::string formQuery(const Document &doc, int form, const std::string &inputName, const std::string &inputValue) {
	std::string q;
	auto add = [&](const std::string &k, const std::string &v) {
		if (k.empty())
			return;
		if (!q.empty())
			q += '&';
		q += net::urlEncode(k) + "=" + net::urlEncode(v);
	};
	if (form >= 0 && form < (int)doc.forms.size())
		for (auto &f : doc.forms[form].fields)
			add(f.first, f.second);
	add(inputName, inputValue);
	return q;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

bool isBlockTag(const std::string &t) {
	static const char *kTags[] = {"p", "div", "section", "article", "header", "footer", "nav", "aside", "main",
	                              "ul", "ol", "dl", "dt", "dd", "table", "tr", "form", "address", "center",
	                              "details", "summary", "fieldset", "figure", "figcaption", "caption", "tbody",
	                              "thead", "tfoot", "menu", "hgroup", "noscript", "legend", "body", "html"};
	for (const char *k : kTags)
		if (t == k)
			return true;
	return false;
}

struct Parser {
	Document &doc;
	std::string base;
	Block cur;
	int bold = 0, heading = 0, pre = 0, quote = 0, link = -1, form = -1;
	bool inHead = false, inTitle = false, lastSpace = true;
	std::vector<int> listCounters; // -1 unordered, else next number
	std::string title;

	explicit Parser(Document &d) : doc(d) {}

	Style style() const {
		if (heading == 1)
			return S_H1;
		if (heading == 2)
			return S_H2;
		if (heading >= 3)
			return link >= 0 ? S_LINK : S_H3;
		if (link >= 0) {
			const Link &l = doc.links[link];
			if (l.submit)
				return S_BUTTON;
			return S_LINK;
		}
		return bold ? S_BOLD : S_NORMAL;
	}

	void flush(bool tight = false) {
		// Trim trailing whitespace
		while (!cur.spans.empty()) {
			std::string &t = cur.spans.back().text;
			while (!t.empty() && t.back() == ' ')
				t.pop_back();
			if (t.empty())
				cur.spans.pop_back();
			else
				break;
		}
		if (!cur.spans.empty() || cur.kind == B_RULE)
			doc.blocks.push_back(cur);
		cur = Block();
		cur.kind = pre ? B_PRE : (quote ? B_QUOTE : (heading ? B_HEADING : B_PARA));
		cur.indent = (int)listCounters.size() + quote;
		cur.tight = tight;
		lastSpace = true;
	}

	void emit(const std::string &text, Style st, int lnk) {
		if (text.empty())
			return;
		if (!cur.spans.empty() && cur.spans.back().style == st && cur.spans.back().link == lnk)
			cur.spans.back().text += text;
		else
			cur.spans.push_back({text, st, lnk});
	}

	void text(const std::string &raw) {
		if (inTitle) {
			title += raw;
			return;
		}
		if (inHead)
			return;
		std::string t = decodeEntities(raw);
		if (pre) {
			emit(t, S_NORMAL, link);
			return;
		}
		std::string out;
		out.reserve(t.size());
		for (char c : t) {
			if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f') {
				if (!lastSpace)
					out += ' ';
				lastSpace = true;
			} else {
				out += c;
				lastSpace = false;
			}
		}
		emit(out, style(), link);
	}

	void inlineText(const std::string &t, Style st, int lnk) {
		if (!lastSpace)
			emit(" ", style(), link);
		emit(t, st, lnk);
		emit(" ", st == S_LINK ? S_NORMAL : st, -1);
		lastSpace = true;
	}

	std::string attr(const std::vector<std::pair<std::string, std::string>> &attrs, const char *name) {
		for (auto &a : attrs)
			if (a.first == name)
				return a.second;
		return "";
	}

	void startTag(const std::string &name, const std::vector<std::pair<std::string, std::string>> &attrs) {
		if (name == "head") {
			inHead = true;
		} else if (name == "body") {
			inHead = false;
		} else if (name == "title") {
			inTitle = true;
		} else if (name == "base") {
			std::string h = attr(attrs, "href");
			if (!h.empty())
				base = resolveUrl(base, h);
		} else if (name == "br") {
			flush(true);
		} else if (name == "hr") {
			flush();
			cur.kind = B_RULE;
			flush();
		} else if (name[0] == 'h' && name.size() == 2 && name[1] >= '1' && name[1] <= '6') {
			flush();
			heading = name[1] - '0';
			cur.kind = B_HEADING;
		} else if (name == "li") {
			flush();
			cur.kind = B_LIST;
			std::string bullet = "\xE2\x80\xA2 ";
			if (!listCounters.empty() && listCounters.back() >= 0)
				bullet = std::to_string(listCounters.back()++) + ". ";
			emit(bullet, S_DIM, -1);
		} else if (name == "ul" || name == "ol" || name == "menu") {
			flush();
			int start = 1;
			std::string s = attr(attrs, "start");
			if (!s.empty())
				start = atoi(s.c_str());
			listCounters.push_back(name == "ol" ? start : -1);
			cur.indent = (int)listCounters.size() + quote;
		} else if (name == "pre") {
			flush();
			pre++;
			cur.kind = B_PRE;
		} else if (name == "blockquote") {
			flush();
			quote++;
			cur.kind = B_QUOTE;
			cur.indent = (int)listCounters.size() + quote;
		} else if (name == "b" || name == "strong" || name == "th") {
			if (name == "th" && !cur.spans.empty())
				emit("  ", S_NORMAL, -1);
			bold++;
		} else if (name == "td") {
			if (!cur.spans.empty() && !lastSpace)
				emit("  ", S_NORMAL, -1);
		} else if (name == "a") {
			std::string href = attr(attrs, "href");
			if (!href.empty() && lower(href).compare(0, 11, "javascript:") != 0) {
				Link l;
				l.href = resolveUrl(base, href);
				doc.links.push_back(l);
				link = (int)doc.links.size() - 1;
			}
		} else if (name == "img") {
			std::string alt = trim(decodeEntities(attr(attrs, "alt")));
			if (!alt.empty())
				inlineText("[" + alt + "]", link >= 0 ? S_LINK : S_DIM, link);
			else if (link >= 0)
				inlineText(TR("[image]", "[imagem]"), S_LINK, link);
		} else if (name == "form") {
			flush();
			Form f;
			std::string action = attr(attrs, "action");
			f.action = action.empty() ? doc.url : resolveUrl(base, action);
			f.post = lower(attr(attrs, "method")) == "post";
			doc.forms.push_back(f);
			form = (int)doc.forms.size() - 1;
		} else if (name == "input" || name == "textarea") {
			std::string type = lower(attr(attrs, "type"));
			std::string nm = attr(attrs, "name");
			std::string value = decodeEntities(attr(attrs, "value"));
			if (type == "hidden") {
				if (form >= 0 && !nm.empty())
					doc.forms[form].fields.emplace_back(nm, value);
			} else if (type == "submit" || type == "image" || type == "button") {
				if (form >= 0 && type != "button") {
					Link l;
					l.form = form;
					l.submit = true;
					l.name = nm;
					l.value = value;
					doc.links.push_back(l);
					inlineText(value.empty() ? std::string(TR("Submit", "Enviar")) : value, S_BUTTON, (int)doc.links.size() - 1);
				}
			} else if (type == "" || type == "text" || type == "search" || type == "email" || type == "url" || type == "password" || type == "tel" || type == "number" || name == "textarea") {
				Link l;
				l.form = form;
				l.text = true;
				l.name = nm;
				l.value = value;
				doc.links.push_back(l);
				std::string ph = decodeEntities(attr(attrs, "placeholder"));
				if (ph.empty())
					ph = attr(attrs, "aria-label");
				std::string shown = value.empty() ? (ph.empty() ? std::string(TR("Type here", "Digite aqui")) : ph) : value;
				inlineText(shown, S_INPUT, (int)doc.links.size() - 1);
			}
		} else if (name == "button") {
			if (form >= 0) {
				Link l;
				l.form = form;
				l.submit = true;
				l.name = attr(attrs, "name");
				l.value = attr(attrs, "value");
				doc.links.push_back(l);
				link = (int)doc.links.size() - 1;
				if (!lastSpace)
					emit(" ", S_NORMAL, -1);
			}
		} else if (name == "q") {
			text("\xE2\x80\x9C");
		}
		if (isBlockTag(name))
			flush();
		if (name == "dd")
			cur.indent++;
	}

	void endTag(const std::string &name) {
		if (name == "head") {
			inHead = false;
		} else if (name == "title") {
			inTitle = false;
		} else if (name[0] == 'h' && name.size() == 2 && name[1] >= '1' && name[1] <= '6') {
			flush();
			heading = 0;
			flush();
		} else if (name == "ul" || name == "ol" || name == "menu") {
			flush();
			if (!listCounters.empty())
				listCounters.pop_back();
			flush();
		} else if (name == "pre") {
			flush();
			if (pre)
				pre--;
			flush();
		} else if (name == "blockquote") {
			flush();
			if (quote)
				quote--;
			flush();
		} else if (name == "b" || name == "strong" || name == "th") {
			if (bold)
				bold--;
		} else if (name == "a" || name == "button") {
			link = -1;
			if (name == "button")
				emit(" ", S_NORMAL, -1);
		} else if (name == "form") {
			flush();
			form = -1;
		} else if (name == "li") {
			flush();
		} else if (name == "q") {
			text("\xE2\x80\x9D");
		}
		if (isBlockTag(name))
			flush();
	}
};

// Content of these elements is never shown
bool skipContent(const std::string &name) {
	return name == "script" || name == "style" || name == "svg" || name == "template" || name == "iframe" ||
	       name == "object" || name == "select" || name == "textarea" || name == "math" || name == "canvas" ||
	       name == "video" || name == "audio" || name == "picture";
}

std::string findCharset(const std::string &src) {
	std::string head = lower(src.substr(0, 4096));
	size_t p = head.find("charset=");
	if (p == std::string::npos)
		return "";
	p += 8;
	while (p < head.size() && (head[p] == '"' || head[p] == '\'' || head[p] == ' '))
		p++;
	size_t e = p;
	while (e < head.size() && (isalnum((unsigned char)head[e]) || head[e] == '-' || head[e] == '_'))
		e++;
	return head.substr(p, e - p);
}

bool isLatin1(const std::string &cs) {
	return cs.find("8859-1") != std::string::npos || cs.find("1252") != std::string::npos ||
	       cs.find("latin") != std::string::npos || cs.find("8859-15") != std::string::npos;
}

} // namespace

void parse(const std::string &sourceIn, const std::string &url, const std::string &charsetIn, Document &doc) {
	doc = Document();
	doc.url = url;
	std::string charset = lower(charsetIn);
	if (charset.empty())
		charset = findCharset(sourceIn);
	const std::string *srcp = &sourceIn;
	std::string converted;
	if (isLatin1(charset) || !validUtf8(sourceIn)) {
		converted = latin1ToUtf8(sourceIn);
		srcp = &converted;
	}
	const std::string &src = *srcp;

	Parser p(doc);
	p.base = url;
	size_t i = 0, n = src.size();
	while (i < n) {
		size_t lt = src.find('<', i);
		if (lt == std::string::npos)
			lt = n;
		if (lt > i)
			p.text(src.substr(i, lt - i));
		if (lt >= n)
			break;
		i = lt;
		if (src.compare(i, 4, "<!--") == 0) {
			size_t e = src.find("-->", i + 4);
			i = e == std::string::npos ? n : e + 3;
			continue;
		}
		if (i + 1 < n && (src[i + 1] == '!' || src[i + 1] == '?')) {
			size_t e = src.find('>', i);
			i = e == std::string::npos ? n : e + 1;
			continue;
		}
		bool end = i + 1 < n && src[i + 1] == '/';
		size_t j = i + (end ? 2 : 1);
		size_t ns = j;
		while (j < n && (isalnum((unsigned char)src[j]) || src[j] == '-' || src[j] == ':'))
			j++;
		if (j == ns) {
			// Not a tag, a literal '<'
			p.text("<");
			i++;
			continue;
		}
		std::string name = lower(src.substr(ns, j - ns));
		std::vector<std::pair<std::string, std::string>> attrs;
		// Attributes
		while (j < n && src[j] != '>') {
			while (j < n && (isspace((unsigned char)src[j]) || src[j] == '/'))
				j++;
			if (j >= n || src[j] == '>')
				break;
			size_t as = j;
			while (j < n && !isspace((unsigned char)src[j]) && src[j] != '=' && src[j] != '>' && src[j] != '/')
				j++;
			std::string an = lower(src.substr(as, j - as));
			std::string av;
			while (j < n && isspace((unsigned char)src[j]))
				j++;
			if (j < n && src[j] == '=') {
				j++;
				while (j < n && isspace((unsigned char)src[j]))
					j++;
				if (j < n && (src[j] == '"' || src[j] == '\'')) {
					char qc = src[j++];
					size_t ve = src.find(qc, j);
					if (ve == std::string::npos)
						ve = n;
					av = src.substr(j, ve - j);
					j = ve + 1;
				} else {
					size_t vs = j;
					while (j < n && !isspace((unsigned char)src[j]) && src[j] != '>')
						j++;
					av = src.substr(vs, j - vs);
				}
			}
			if (!an.empty())
				attrs.emplace_back(an, decodeEntities(av));
		}
		i = j < n ? j + 1 : n;
		if (end) {
			p.endTag(name);
			continue;
		}
		if (skipContent(name)) {
			if (name == "textarea")
				p.startTag(name, attrs);
			// Jump to the closing tag
			std::string lowerRest;
			size_t k = i;
			while (true) {
				size_t c = src.find("</", k);
				if (c == std::string::npos) {
					i = n;
					break;
				}
				if (lower(src.substr(c + 2, name.size())) == name) {
					size_t e = src.find('>', c);
					i = e == std::string::npos ? n : e + 1;
					break;
				}
				k = c + 2;
			}
			continue;
		}
		p.startTag(name, attrs);
	}
	p.flush();

	doc.title = decodeEntities(p.title);
	// Collapse whitespace in the title
	std::string t;
	bool sp = true;
	for (char c : doc.title) {
		if (isspace((unsigned char)c)) {
			if (!sp)
				t += ' ';
			sp = true;
		} else {
			t += c;
			sp = false;
		}
	}
	while (!t.empty() && t.back() == ' ')
		t.pop_back();
	doc.title = t;
}

void parsePlainText(const std::string &source, const std::string &url, Document &doc) {
	doc = Document();
	doc.url = url;
	const std::string src = validUtf8(source) ? source : latin1ToUtf8(source);
	size_t i = 0;
	while (i <= src.size()) {
		size_t e = src.find('\n', i);
		if (e == std::string::npos)
			e = src.size();
		std::string line = src.substr(i, e - i);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		Block b;
		b.kind = B_PRE;
		b.tight = true;
		if (line.empty())
			line = " ";
		b.spans.push_back({line, S_NORMAL, -1});
		doc.blocks.push_back(b);
		i = e + 1;
	}
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

static const Font &fontFor(Style s) {
	switch (s) {
		case S_H1:
			return fonts::head;
		case S_H2:
			return fonts::title;
		case S_H3:
		case S_BOLD:
		case S_BUTTON:
			return fonts::bold;
		default:
			return fonts::body;
	}
}

void layout(const Document &doc, int width, Layout &out) {
	out.lines.clear();
	int y = 4;
	for (const Block &b : doc.blocks) {
		if (b.kind == B_RULE) {
			Line l;
			l.y = y + 2;
			l.height = 8;
			l.indent = 0;
			l.kind = B_RULE;
			out.lines.push_back(l);
			y += 12;
			continue;
		}
		if (!b.tight)
			y += b.kind == B_HEADING ? 6 : (b.kind == B_LIST ? 1 : 5);
		int x0 = b.indent * 12;
		if (b.kind == B_QUOTE)
			x0 += 6;
		if (x0 > width / 3)
			x0 = width / 3;
		int maxX = width;

		Line line;
		line.y = y;
		line.height = fonts::body.lineHeight();
		line.indent = x0;
		line.kind = b.kind;
		int x = x0;
		auto newLine = [&]() {
			out.lines.push_back(line);
			y += line.height + 1;
			line = Line();
			line.y = y;
			line.height = fonts::body.lineHeight();
			line.indent = x0;
			line.kind = b.kind;
			// Continuation lines of list items are indented under the text
			x = x0 + (b.kind == B_LIST ? 10 : 0);
		};
		auto place = [&](const std::string &piece, Style st, int link, int w) {
			if (!line.spans.empty() && line.spans.back().style == st && line.spans.back().link == link &&
			    line.spans.back().x + fontFor(st).width(line.spans.back().text) == x) {
				line.spans.back().text += piece;
			} else {
				line.spans.push_back({x, piece, st, link});
			}
			x += w;
			int h = fontFor(st).lineHeight();
			if (h > line.height)
				line.height = h;
		};

		for (const Span &sp : b.spans) {
			const Font &f = fontFor(sp.style);
			const std::string &t = sp.text;
			size_t i = 0;
			while (i < t.size()) {
				if (b.kind == B_PRE && t[i] == '\n') {
					newLine();
					i++;
					continue;
				}
				// Next word, including its trailing spaces
				size_t e = i;
				while (e < t.size() && t[e] != ' ' && !(b.kind == B_PRE && t[e] == '\n'))
					e++;
				size_t ws = e;
				while (e < t.size() && t[e] == ' ')
					e++;
				std::string word = t.substr(i, ws - i);
				std::string spaces = t.substr(ws, e - ws);
				int ww = f.width(word);
				if (x + ww > maxX && x > line.indent) {
					newLine();
					if (word.empty()) {
						i = e;
						continue;
					}
				}
				if (ww > maxX - x) {
					// Word longer than a line: break it
					const char *p = word.c_str();
					const char *pe = p + word.size();
					std::string chunk;
					int cw = 0;
					while (p < pe) {
						const char *before = p;
						uint32_t cp = utf8Next(p, pe);
						int a = f.advance(cp);
						if (x + cw + a > maxX && !chunk.empty()) {
							place(chunk, sp.style, sp.link, cw);
							newLine();
							chunk.clear();
							cw = 0;
						}
						chunk.append(before, p - before);
						cw += a;
					}
					place(chunk, sp.style, sp.link, cw);
				} else {
					place(word, sp.style, sp.link, ww);
				}
				if (!spaces.empty() && x > line.indent) {
					// Spaces inside a link/box keep its style, trailing ones don't
					bool inner = e < t.size();
					bool special = sp.style == S_LINK || sp.style == S_INPUT || sp.style == S_BUTTON;
					place(spaces, special && !inner ? S_NORMAL : sp.style, special && !inner ? -1 : sp.link, f.width(spaces));
				}
				i = e;
			}
		}
		if (!line.spans.empty())
			newLine();
	}
	out.height = y + 8;
}

} // namespace html

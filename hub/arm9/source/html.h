// Minimal HTML to text converter and layout engine for the Hub browser.
// It keeps the structure that matters on a 256x192 screen: headings,
// paragraphs, lists, links, simple forms (search boxes) and image alt texts.
#pragma once

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace html {

enum Style : uint8_t {
	S_NORMAL,
	S_BOLD,
	S_H1,
	S_H2,
	S_H3,
	S_LINK,
	S_DIM,   // image placeholders, captions
	S_INPUT, // text box
	S_BUTTON,
};

enum BlockKind : uint8_t { B_PARA, B_HEADING, B_LIST, B_QUOTE, B_PRE, B_RULE };

struct Span {
	std::string text;
	Style style;
	int link; // index in Document::links, -1 if none
};

struct Block {
	BlockKind kind = B_PARA;
	int indent = 0;     // nesting level (lists, quotes)
	bool tight = false; // continues the previous block after a <br>
	std::vector<Span> spans;
};

struct Link {
	std::string href;     // absolute URL
	int form = -1;        // >= 0: form element
	std::string name;     // input name
	std::string value;    // input value
	bool submit = false;  // submit button
	bool text = false;    // text input
};

struct Form {
	std::string action;
	bool post = false;
	std::vector<std::pair<std::string, std::string>> fields; // hidden fields
};

struct Document {
	std::string url;
	std::string title;
	std::vector<Block> blocks;
	std::vector<Link> links;
	std::vector<Form> forms;
};

// charset: from the Content-Type header, may be empty
void parse(const std::string &source, const std::string &url, const std::string &charset, Document &doc);
void parsePlainText(const std::string &source, const std::string &url, Document &doc);

std::string resolveUrl(const std::string &base, const std::string &href);
std::string decodeEntities(const std::string &s);
std::string latin1ToUtf8(const std::string &s);
// Builds the URL/POST data of a form submission
std::string formQuery(const Document &doc, int form, const std::string &inputName, const std::string &inputValue);

// Layout ---------------------------------------------------------------------

struct LineSpan {
	int x;
	std::string text;
	Style style;
	int link;
};

struct Line {
	int y;
	int height;
	int indent;
	BlockKind kind;
	std::vector<LineSpan> spans;
};

struct Layout {
	std::vector<Line> lines;
	int height = 0;
};

void layout(const Document &doc, int width, Layout &out);

} // namespace html

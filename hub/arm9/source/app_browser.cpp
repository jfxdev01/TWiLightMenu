// Web browser: a text-mode browser that works well on a 256x192 screen.
//
// - HTTP and HTTPS (Mbed TLS, certificates verified against a built-in set of
//   root certificates, with the option to continue on errors)
// - Two-screen continuous reading, D-pad/stylus link selection
// - Address bar with search, bookmarks, history, simple forms (GET/POST)
// - "Reader mode" through a proxy that simplifies heavy pages
// - Opens images and downloads files (e.g. .nds homebrew) to /downloads

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "apps.h"
#include "config.h"
#include "html.h"
#include "i18n.h"
#include "image.h"
#include "net.h"
#include "platform.h"
#include "ui.h"

namespace apps {

namespace {

const uint16_t kColor = HEXCOLOR(0x00AFC8);
const int kContentX = 8, kContentW = 240;
const int kTopContentY = 50, kTopContentH = 192 - 50;
const int kBottomContentH = 168;

struct SearchEngine {
	const char *name;
	const char *url; // query appended
};

const SearchEngine kEngines[] = {
	{"DuckDuckGo Lite", "https://lite.duckduckgo.com/lite/?q="},
	{"Wikipedia", "https://%s.m.wikipedia.org/w/index.php?search="},
	{"FrogFind", "http://frogfind.com/?q="},
	{"Google", "https://www.google.com/search?gbv=1&q="},
};
const int kEngineCount = sizeof(kEngines) / sizeof(kEngines[0]);

struct DefaultBookmark {
	const char *title, *url;
};

const DefaultBookmark kDefaultBookmarks[] = {
	{"DuckDuckGo Lite", "https://lite.duckduckgo.com/lite/"},
	{"Wikipedia (m)", "https://pt.m.wikipedia.org/"},
	{"68k.news", "http://68k.news/"},
	{"CNN Lite", "https://lite.cnn.com/"},
	{"NPR Text", "https://text.npr.org/"},
	{"Hacker News", "https://news.ycombinator.com/"},
	{"DS-Homebrew Wiki", "https://wiki.ds-homebrew.com/"},
	{"FrogFind", "http://frogfind.com/"},
};

struct HistoryEntry {
	std::string url;
	std::string post;
	int scroll;
};

class Browser {
public:
	void run(const std::string &startUrl);

private:
	html::Document doc;
	html::Layout lay;
	std::vector<HistoryEntry> history;
	std::string url, post;
	int view = 0;        // document y at the top of the top screen content area
	int bottomStart = 0; // document y at the top of the bottom screen
	int focus = -1;      // index into visibleLinks
	bool verified = false, secure = false, readerMode = false;
	std::vector<std::string> insecureHosts;
	std::string status;

	struct VisibleLink {
		int link;
		int line, span;
		ui::Rect rect; // bottom screen rect (y < 0: top screen)
		bool onTop;
	};
	std::vector<VisibleLink> visible;

	void navigate(const std::string &target, const std::string &postData = "", bool addHistory = true);
	bool load(const std::string &target, const std::string &postData);
	void home();
	void back();
	void draw();
	void drawContent(Surface &s, int docY, int screenY, int height, bool top);
	void collectVisibleLinks();
	void computeSplit();
	void activateLink(int linkIndex);
	void addressBar();
	void menu();
	void bookmarks();
	std::string searchUrl(const std::string &query);
	std::string bookmarkPageHtml();
	std::string hostOf(const std::string &u);
	int maxView() const;
};

std::string Browser::hostOf(const std::string &u) {
	size_t s = u.find("://");
	if (s == std::string::npos)
		return u;
	size_t e = u.find_first_of("/?#", s + 3);
	return u.substr(s + 3, e == std::string::npos ? std::string::npos : e - s - 3);
}

int Browser::maxView() const {
	int m = lay.height - (kTopContentH + kBottomContentH);
	return m < 0 ? 0 : m;
}

std::string Browser::searchUrl(const std::string &query) {
	int e = config::ini().getInt("BROWSER", "SEARCH_ENGINE", 0);
	if (e < 0 || e >= kEngineCount)
		e = 0;
	std::string base = kEngines[e].url;
	size_t s = base.find("%s");
	if (s != std::string::npos)
		base.replace(s, 2, i18n::isPt() ? "pt" : "en");
	return base + net::urlEncode(query);
}

static std::vector<std::pair<std::string, std::string>> loadBookmarks() {
	auto entries = config::ini().entries("BOOKMARKS");
	if (entries.empty() && config::ini().getInt("BROWSER", "BOOKMARKS_INIT", 0) == 0) {
		for (const DefaultBookmark &b : kDefaultBookmarks)
			config::ini().set("BOOKMARKS", b.title, b.url);
		config::ini().setInt("BROWSER", "BOOKMARKS_INIT", 1);
		config::save();
		entries = config::ini().entries("BOOKMARKS");
	}
	return entries;
}

std::string Browser::bookmarkPageHtml() {
	std::string h = "<html><head><title>";
	h += TR("Start", "Início");
	h += "</title></head><body><h1>";
	h += TR("Where to?", "Para onde?");
	h += "</h1><form action=\"about:go\"><input type=\"search\" name=\"q\" placeholder=\"";
	h += TR("Search or type an address", "Pesquise ou digite um endereço");
	h += "\"></form><h2>";
	h += TR("Bookmarks", "Favoritos");
	h += "</h2><ul>";
	for (auto &b : loadBookmarks())
		h += "<li><a href=\"" + b.second + "\">" + b.first + "</a></li>";
	h += "</ul><hr><p>";
	h += TR("Tips: X opens the address bar, Y opens the menu (bookmarks, reader mode, search engine). "
	        "Use the D-pad or the stylus to pick links, L/R to change page.",
	        "Dicas: X abre a barra de endereço, Y abre o menu (favoritos, modo leitura, buscador). "
	        "Use o direcional ou a caneta para escolher links e L/R para mudar de página.");
	h += "</p><p>";
	h += TR("Heavy pages load faster in reader mode.", "Páginas pesadas carregam mais rápido no modo leitura.");
	h += "</p></body></html>";
	return h;
}

void Browser::home() {
	url = "about:home";
	post.clear();
	html::parse(bookmarkPageHtml(), url, "utf-8", doc);
	html::layout(doc, kContentW, lay);
	view = 0;
	focus = -1;
	secure = verified = false;
	status.clear();
}

static bool looksLikeDownload(const std::string &u) {
	std::string path = u.substr(0, u.find_first_of("?#"));
	size_t dot = path.rfind('.');
	if (dot == std::string::npos || path.rfind('/') > dot)
		return false;
	std::string ext = path.substr(dot + 1);
	for (char &c : ext)
		c = (char)tolower((unsigned char)c);
	static const char *kExts[] = {"nds", "dsi", "srl", "app", "cia", "zip", "7z", "rar", "gba", "gb", "gbc", "nes", "sfc", "smc",
	                              "gen", "md", "sms", "gg", "pce", "mp3", "wav", "ogg", "mod", "xm", "pdf", "bin", "firm", "3dsx", "tmd", "lut"};
	for (const char *e : kExts)
		if (ext == e)
			return true;
	return false;
}

static std::string fileNameFromUrl(const std::string &u) {
	std::string path = u.substr(0, u.find_first_of("?#"));
	std::string name = path.substr(path.rfind('/') + 1);
	std::string clean;
	for (char c : name) {
		if (c == '%' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '\\')
			clean += '_';
		else
			clean += c;
	}
	return clean.empty() ? "download.bin" : clean;
}

static void downloadFile(const std::string &u) {
	std::string dir = platform::root() + "/downloads";
	mkdir(dir.c_str(), 0777);
	std::string dest = dir + "/" + fileNameFromUrl(u);
	if (!ui::confirm(TR("Download file?", "Baixar arquivo?"), fileNameFromUrl(u) + "\n" + TR("It will be saved in /downloads", "Será salvo em /downloads"),
	                 TR("Download", "Baixar"), TR("Cancel", "Cancelar")))
		return;
	net::Request req;
	req.url = u;
	req.outputFile = dest;
	req.maxBytes = 256u * 1024 * 1024;
	net::Response res;
	if (net::fetchWithUi(TR("Downloading", "Baixando"), req, res) && res.status < 400) {
		platform::playSfx(platform::SFX_SELECT);
		ui::message(TR("Download complete", "Download concluído"), dest);
	} else {
		remove(dest.c_str());
		platform::playSfx(platform::SFX_ERROR);
		ui::message(TR("Download failed", "Falha no download"), res.error.empty() ? "HTTP " + std::to_string(res.status) : res.error);
	}
}

bool Browser::load(const std::string &target, const std::string &postData) {
	if (target == "about:home") {
		home();
		return true;
	}
	if (target.compare(0, 7, "http://") != 0 && target.compare(0, 8, "https://") != 0) {
		ui::message(TR("Unsupported address", "Endereço não suportado"), target);
		return false;
	}
	if (looksLikeDownload(target) && postData.empty()) {
		if (!net::ensureConnected())
			return false;
		downloadFile(target);
		return false;
	}
	if (!net::ensureConnected())
		return false;

	std::string fetchUrl = target;
	if (readerMode && postData.empty() && hostOf(target) != "frogfind.com")
		fetchUrl = "http://frogfind.com/read.php?a=" + target;

	net::Request req;
	req.url = fetchUrl;
	req.postData = postData;
	req.maxBytes = platform::isDSi() ? 3 * 1024 * 1024 : 768 * 1024;
	std::string host = hostOf(fetchUrl);
	for (const std::string &h : insecureHosts)
		if (h == host)
			req.verifyTls = false;
	net::Response res;
	bool ok = net::fetchWithUi(TR("Loading", "Carregando"), req, res);
	if (!ok && res.certError) {
		if (ui::confirm(TR("Certificate not verified", "Certificado não verificado"),
		                TR("The identity of this site couldn't be verified. Only continue if you trust it.",
		                   "Não foi possível verificar a identidade deste site. Continue só se confiar nele."),
		                TR("Continue", "Continuar"), TR("Cancel", "Cancelar"))) {
			insecureHosts.push_back(host);
			req.verifyTls = false;
			ok = net::fetchWithUi(TR("Loading", "Carregando"), req, res);
		} else {
			return false;
		}
	}
	if (!ok) {
		platform::playSfx(platform::SFX_ERROR);
		ui::message(TR("Couldn't load the page", "Não foi possível carregar a página"), res.error);
		return false;
	}

	std::string ct = res.contentType;
	for (char &c : ct)
		c = (char)tolower((unsigned char)c);
	std::string finalUrl = res.url.empty() ? fetchUrl : res.url;
	if (ct.compare(0, 6, "image/") == 0) {
		Image img;
		if (image::decode((const uint8_t *)res.body.data(), res.body.size(), img, 1024)) {
			char info[32];
			snprintf(info, sizeof(info), "%dx%d", img.w, img.h);
			image::view(img, fileNameFromUrl(finalUrl), info);
		} else {
			ui::message(TR("Image", "Imagem"), TR("This image format isn't supported.", "Este formato de imagem não é suportado."));
		}
		return false;
	}
	bool isHtml = ct.empty() || ct.find("html") != std::string::npos || ct.find("xml") != std::string::npos;
	bool isText = ct.compare(0, 5, "text/") == 0 || ct.find("json") != std::string::npos || ct.find("javascript") != std::string::npos;
	if (!isHtml && !isText) {
		downloadFile(finalUrl);
		return false;
	}
	std::string charset;
	size_t cs = ct.find("charset=");
	if (cs != std::string::npos)
		charset = ct.substr(cs + 8);
	if (isHtml)
		html::parse(res.body, finalUrl, charset, doc);
	else
		html::parsePlainText(res.body, finalUrl, doc);
	res.body.clear();
	res.body.shrink_to_fit();
	html::layout(doc, kContentW, lay);
	url = finalUrl;
	post = postData;
	secure = finalUrl.compare(0, 8, "https://") == 0;
	verified = secure && res.verified && req.verifyTls;
	view = 0;
	focus = -1;
	status = res.truncated ? TR("Page too big, showing the beginning", "Página grande demais, mostrando o início") : "";
	if (res.status >= 400)
		status = "HTTP " + std::to_string(res.status);
	return true;
}

void Browser::navigate(const std::string &target, const std::string &postData, bool addHistory) {
	HistoryEntry cur{url, post, view};
	if (load(target, postData)) {
		if (addHistory && !cur.url.empty())
			history.push_back(cur);
		if (history.size() > 30)
			history.erase(history.begin());
		config::ini().set("BROWSER", "LAST_URL", url.compare(0, 6, "about:") == 0 ? "" : url);
	}
}

void Browser::back() {
	if (history.empty()) {
		platform::playSfx(platform::SFX_ERROR);
		return;
	}
	HistoryEntry h = history.back();
	history.pop_back();
	platform::playSfx(platform::SFX_BACK);
	if (load(h.url, h.post)) {
		view = h.scroll;
		if (view > maxView())
			view = maxView();
	}
}

void Browser::activateLink(int li) {
	if (li < 0 || li >= (int)doc.links.size())
		return;
	html::Link &l = doc.links[li];
	platform::playSfx(platform::SFX_SELECT);
	if (l.form < 0) {
		navigate(l.href);
		return;
	}
	const html::Form &f = doc.forms[l.form];
	std::string name, value;
	if (l.text) {
		value = l.value;
		if (!ui::keyboard(TR("Type the text", "Digite o texto"), value, 256, false))
			return;
		l.value = value;
		name = l.name;
	} else {
		// Submit button: send the first text field of the form, if any
		for (const html::Link &o : doc.links) {
			if (o.form == l.form && o.text) {
				name = o.name;
				value = o.value;
				break;
			}
		}
	}
	if (f.action == "about:go") {
		std::string input = value;
		if (input.empty())
			return;
		bool isUrl = input.find(' ') == std::string::npos && (input.find('.') != std::string::npos || input.find("://") != std::string::npos);
		if (isUrl && input.find("://") == std::string::npos)
			input = "https://" + input;
		navigate(isUrl ? input : searchUrl(input));
		return;
	}
	std::string q = html::formQuery(doc, l.form, name, value);
	if (l.submit && !l.name.empty())
		q += (q.empty() ? "" : "&") + net::urlEncode(l.name) + "=" + net::urlEncode(l.value);
	if (f.post) {
		navigate(f.action, q);
	} else {
		std::string a = f.action.substr(0, f.action.find('#'));
		size_t qm = a.find('?');
		if (qm != std::string::npos)
			a = a.substr(0, qm);
		navigate(a + "?" + q);
	}
}

void Browser::addressBar() {
	std::string input = url.compare(0, 6, "about:") == 0 ? "" : url;
	if (!ui::keyboard(TR("Address or search", "Endereço ou pesquisa"), input, 512, false, "https://"))
		return;
	while (!input.empty() && input.back() == ' ')
		input.pop_back();
	if (input.empty())
		return;
	bool isUrl = input.find(' ') == std::string::npos && (input.find('.') != std::string::npos || input.find("://") != std::string::npos || input.compare(0, 10, "localhost") == 0);
	if (isUrl && input.find("://") == std::string::npos)
		input = "https://" + input;
	navigate(isUrl ? input : searchUrl(input));
}

void Browser::bookmarks() {
	auto list = loadBookmarks();
	if (list.empty()) {
		ui::message(TR("Bookmarks", "Favoritos"), TR("No bookmarks yet.", "Nenhum favorito ainda."));
		return;
	}
	std::vector<std::string> names;
	for (auto &b : list)
		names.push_back(b.first);
	int c = ui::choose(TR("Bookmarks", "Favoritos"), names, 0);
	if (c >= 0)
		navigate(list[c].second);
}

void Browser::menu() {
	std::vector<std::string> items = {
		TR("Start page", "Página inicial"),
		TR("Bookmarks", "Favoritos"),
		TR("Add bookmark", "Adicionar aos favoritos"),
		TR("Remove a bookmark", "Remover um favorito"),
		TR("Reload", "Recarregar"),
		std::string(TR("Reader mode: ", "Modo leitura: ")) + (readerMode ? TR("on", "ligado") : TR("off", "desligado")),
		std::string(TR("Search engine: ", "Buscador: ")) + kEngines[config::ini().getInt("BROWSER", "SEARCH_ENGINE", 0) % kEngineCount].name,
		TR("Download this address", "Baixar este endereço"),
	};
	int c = ui::choose(TR("Browser", "Navegador"), items, 0);
	switch (c) {
		case 0:
			navigate("about:home");
			break;
		case 1:
			bookmarks();
			break;
		case 2: {
			if (url.compare(0, 6, "about:") == 0)
				break;
			std::string title = doc.title.empty() ? hostOf(url) : doc.title;
			for (char &ch : title)
				if (ch == '=' || ch == '[' || ch == ']')
					ch = ' ';
			if (ui::keyboard(TR("Bookmark name", "Nome do favorito"), title, 60)) {
				config::ini().set("BOOKMARKS", title, url);
				config::save();
				ui::toast(TR("Bookmark added", "Favorito adicionado"));
			}
			break;
		}
		case 3: {
			auto list = loadBookmarks();
			std::vector<std::string> names;
			for (auto &b : list)
				names.push_back(b.first);
			int r = ui::choose(TR("Remove a bookmark", "Remover um favorito"), names, 0);
			if (r >= 0) {
				config::ini().remove("BOOKMARKS", list[r].first);
				config::save();
				ui::toast(TR("Bookmark removed", "Favorito removido"));
				if (url == "about:home")
					home();
			}
			break;
		}
		case 4:
			if (url.compare(0, 6, "about:") == 0)
				home();
			else
				load(url, post);
			break;
		case 5:
			readerMode = !readerMode;
			config::ini().setInt("BROWSER", "READER_MODE", readerMode);
			config::save();
			if (url.compare(0, 6, "about:") != 0)
				load(url, post);
			break;
		case 6: {
			std::vector<std::string> names;
			for (const SearchEngine &e : kEngines)
				names.push_back(e.name);
			int e = ui::choose(TR("Search engine", "Buscador"), names, config::ini().getInt("BROWSER", "SEARCH_ENGINE", 0));
			if (e >= 0) {
				config::ini().setInt("BROWSER", "SEARCH_ENGINE", e);
				config::save();
			}
			break;
		}
		case 7:
			if (url.compare(0, 6, "about:") != 0 && net::ensureConnected())
				downloadFile(url);
			break;
	}
}

// Draws the lines of the document between docY and docY+height at screenY
void Browser::drawContent(Surface &s, int docY, int screenY, int height, bool top) {
	const ui::Palette &p = ui::pal();
	s.setClip(0, screenY, 256, height);
	int focusLink = focus >= 0 && focus < (int)visible.size() ? visible[focus].link : -1;
	for (size_t li = 0; li < lay.lines.size(); li++) {
		const html::Line &line = lay.lines[li];
		if (line.y + line.height < docY)
			continue;
		if (line.y > docY + height)
			break;
		int y = screenY + line.y - docY;
		if (line.kind == html::B_RULE) {
			gfx::hline(s, kContentX, y + 4, kContentW, p.divider);
			continue;
		}
		if (line.kind == html::B_QUOTE)
			gfx::fillRect(s, kContentX + line.indent - 5, y, 2, line.height + 1, p.accent, 160);
		if (line.kind == html::B_PRE)
			gfx::fillRect(s, kContentX - 3, y, kContentW + 6, line.height + 1, p.panel);
		for (const html::LineSpan &sp : line.spans) {
			const Font &f = sp.style == html::S_H1 ? fonts::head : sp.style == html::S_H2 ? fonts::title
			                : (sp.style == html::S_H3 || sp.style == html::S_BOLD || sp.style == html::S_BUTTON) ? fonts::bold : fonts::body;
			int x = kContentX + sp.x;
			int ty = y + (line.height - f.lineHeight());
			int w = f.width(sp.text);
			uint16_t col = p.text;
			if (sp.style == html::S_LINK)
				col = p.accent;
			else if (sp.style == html::S_DIM)
				col = p.textDim;
			if (sp.link >= 0 && sp.link == focusLink)
				gfx::fillRoundRect(s, x - 2, ty - 1, w + 4, f.lineHeight() + 2, 4, p.accent, 70);
			if (sp.style == html::S_INPUT) {
				gfx::fillRoundRect(s, x - 2, ty - 1, w + 4, f.lineHeight() + 2, 4, p.panel);
				gfx::strokeRoundRect(s, x - 2, ty - 1, w + 4, f.lineHeight() + 2, 4, 1, p.divider);
				col = p.textDim;
			} else if (sp.style == html::S_BUTTON) {
				gfx::fillRoundRect(s, x - 2, ty - 1, w + 4, f.lineHeight() + 2, 4, p.panelAlt);
			}
			f.draw(s, x, ty, sp.text, col);
			if (sp.style == html::S_LINK && sp.text.find_first_not_of(' ') != std::string::npos)
				gfx::hline(s, x, ty + f.ascent() + 2, w, col, 110);
		}
	}
	s.resetClip();
	(void)top;
}

// Lines never straddle the two screens: the bottom screen starts at the first
// line that doesn't fit completely on the top one.
void Browser::computeSplit() {
	bottomStart = view + kTopContentH;
	for (const html::Line &line : lay.lines) {
		if (line.y + line.height > view + kTopContentH) {
			if (line.y >= view && line.y < bottomStart)
				bottomStart = line.y;
			break;
		}
	}
}

void Browser::collectVisibleLinks() {
	computeSplit();
	int prevLink = focus >= 0 && focus < (int)visible.size() ? visible[focus].link : -1;
	visible.clear();
	int end = bottomStart + kBottomContentH;
	for (size_t li = 0; li < lay.lines.size(); li++) {
		const html::Line &line = lay.lines[li];
		if (line.y + line.height <= view)
			continue;
		if (line.y >= end)
			break;
		for (size_t si = 0; si < line.spans.size(); si++) {
			const html::LineSpan &sp = line.spans[si];
			if (sp.link < 0 || sp.text.find_first_not_of(' ') == std::string::npos)
				continue;
			// Merge with the previous piece of the same link on the same line
			if (!visible.empty() && visible.back().link == sp.link && visible.back().line == (int)li)
				continue;
			const Font &f = sp.style == html::S_BUTTON ? fonts::bold : fonts::body;
			VisibleLink v;
			v.link = sp.link;
			v.line = (int)li;
			v.span = (int)si;
			bool onTop = line.y < bottomStart;
			v.onTop = onTop;
			int sy = onTop ? kTopContentY + line.y - view : line.y - bottomStart;
			v.rect = {kContentX + sp.x - 2, sy - 1, f.width(sp.text) + 4, line.height + 2};
			visible.push_back(v);
		}
	}
	focus = -1;
	for (size_t i = 0; i < visible.size(); i++)
		if (visible[i].link == prevLink) {
			focus = (int)i;
			break;
		}
}

void Browser::draw() {
	Surface &top = platform::top();
	Surface &bottom = platform::bottom();
	const ui::Palette &p = ui::pal();

	// Top screen: status bar, address bar, then the page
	ui::background(top);
	drawContent(top, view, kTopContentY, bottomStart - view, true);
	ui::statusBar(top);
	ui::Rect bar{6, 30, 244, 18};
	gfx::fillRoundRect(top, bar.x, bar.y, bar.w, bar.h, 9, p.panel);
	int tx = bar.x + 8;
	if (secure) {
		ui::icon(top, ICON_LOCK, ICONSIZE_16, bar.x + 4, bar.y + 1, verified ? p.success : p.warning);
		tx = bar.x + 22;
	}
	std::string shown = url.compare(0, 6, "about:") == 0 ? std::string(TR("Search or type an address", "Pesquise ou digite um endereço")) : url;
	size_t sch = shown.find("://");
	if (sch != std::string::npos)
		shown = shown.substr(sch + 3);
	if (readerMode && url.compare(0, 6, "about:") != 0)
		shown = std::string(TR("[reader] ", "[leitura] ")) + shown;
	fonts::body.drawEllipsized(top, tx, bar.y + 2, shown, bar.x + bar.w - tx - 8, url.compare(0, 6, "about:") == 0 ? p.textDim : p.text);
	gfx::hline(top, 0, kTopContentY - 2, 256, p.divider);
	ui::drawToast(top);

	// Bottom screen: continuation of the page
	ui::background(bottom);
	drawContent(bottom, bottomStart, 0, kBottomContentH, false);
	if (lay.height > kTopContentH + kBottomContentH) {
		int total = lay.height;
		int barH = kBottomContentH * (kTopContentH + kBottomContentH) / total;
		if (barH < 10)
			barH = 10;
		int y = view * (kBottomContentH - barH) / (maxView() > 0 ? maxView() : 1);
		gfx::fillRoundRect(bottom, 251, y, 3, barH, 1, p.textDim, 150);
	}
	std::string left = !status.empty() ? status : doc.title;
	ui::footer(bottom, {{"B", TR("Back", "Voltar"), KEY_B}, {"X", TR("Address", "Endereço"), KEY_X}, {"Y", TR("Menu", "Menu"), KEY_Y}}, left);
}

void Browser::run(const std::string &startUrl) {
	readerMode = config::ini().getInt("BROWSER", "READER_MODE", 0) != 0;
	home();
	if (!startUrl.empty())
		navigate(startUrl);

	int dragStartY = 0, dragStartView = 0;
	bool dragging = false, pressed = false;
	int pressLink = -1;

	while (true) {
		platform::scanInput();
		net::update();
		collectVisibleLinks();
		draw();

		uint32_t kd = platform::keysDown() | ui::footerTapped();
		uint32_t kr = platform::keysRepeat();
		uint32_t kh = platform::keysHeld();
		int oldView = view;

		if (kh & KEY_DOWN)
			view += (kh & KEY_R) ? 12 : 4;
		if (kh & KEY_UP)
			view -= (kh & KEY_R) ? 12 : 4;
		if (kr & KEY_R && !(kh & (KEY_UP | KEY_DOWN)))
			view += kTopContentH + kBottomContentH - 24;
		if (kr & KEY_L)
			view -= kTopContentH + kBottomContentH - 24;
		if (view > maxView())
			view = maxView();
		if (view < 0)
			view = 0;
		if (view != oldView)
			collectVisibleLinks();

		if (kr & (KEY_LEFT | KEY_RIGHT)) {
			if (!visible.empty()) {
				if (focus < 0)
					focus = (kr & KEY_RIGHT) ? 0 : (int)visible.size() - 1;
				else
					focus = (focus + ((kr & KEY_RIGHT) ? 1 : (int)visible.size() - 1)) % (int)visible.size();
				platform::playSfx(platform::SFX_MOVE);
			}
		}

		// Stylus: drag to scroll, tap a link
		if (platform::touchDown() && platform::touchY() < kBottomContentH) {
			pressed = true;
			dragging = false;
			dragStartY = platform::touchY();
			dragStartView = view;
			pressLink = -1;
			for (const VisibleLink &v : visible)
				if (!v.onTop && v.rect.contains(platform::touchX(), platform::touchY()))
					pressLink = v.link;
		}
		if (pressed && platform::touching()) {
			int dy = platform::touchY() - dragStartY;
			if (dy > 5 || dy < -5)
				dragging = true;
			if (dragging) {
				view = dragStartView - dy;
				if (view > maxView())
					view = maxView();
				if (view < 0)
					view = 0;
			}
		}
		if (pressed && platform::touchReleased()) {
			pressed = false;
			if (!dragging && pressLink >= 0) {
				activateLink(pressLink);
				continue;
			}
		}

		if ((kd & KEY_A) && focus >= 0 && focus < (int)visible.size()) {
			activateLink(visible[focus].link);
			continue;
		}
		if (kd & KEY_X) {
			addressBar();
			continue;
		}
		if (kd & KEY_Y) {
			menu();
			continue;
		}
		if (kd & KEY_SELECT) {
			bookmarks();
			continue;
		}
		if (kd & KEY_B) {
			if (history.empty()) {
				platform::playSfx(platform::SFX_BACK);
				config::save();
				return;
			}
			back();
			continue;
		}
		if (kd & KEY_START) {
			platform::playSfx(platform::SFX_BACK);
			config::save();
			return;
		}
		platform::present();
	}
}

} // namespace

void browser(const std::string &url) {
	Browser *b = new Browser();
	b->run(url);
	delete b;
}

} // namespace apps

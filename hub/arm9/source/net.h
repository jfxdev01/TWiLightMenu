// Networking for TWiLight Hub: WiFi connection management (DSi WPA2 or DS
// mode), HTTP(S) through libcurl + Mbed TLS, and NTP.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <time.h>
#include <vector>

namespace net {

enum State {
	STATE_OFF,        // library not initialised or disconnected
	STATE_SCANNING,   // looking for saved networks
	STATE_CONNECTING, // associating / DHCP
	STATE_CONNECTED,
	STATE_FAILED,
};

struct AccessPoint {
	std::string ssid;
	int rssi;       // DSi: dBm; DS: 0..255
	int bars;       // 0..3
	bool secured;
	bool supported; // can we connect to it at all
	bool saved;     // password stored in hub.ini or in the console settings
	std::string security; // "Open", "WEP", "WPA2"...
	int index;      // index in the driver's list
};

bool init();
bool initialized();
bool dsiMode(); // WiFi running in DSi mode (WPA2 capable)
std::string initError();

State state();
int signalBars(); // -1 when not connected, else 0..3
std::string ssid();
std::string ipAddress();
std::string gateway();
std::string dns();
std::string macAddress();

// Starts connecting in the background (saved networks, then the console's
// connection settings). Call update() every frame.
void connectAuto();
void connectTo(const AccessPoint &ap, const std::string &password);
void disconnect();
void update();
std::string stateText();

// Scanning
void startScan();
std::vector<AccessPoint> scanResults();

// Saved networks (hub.ini)
void savePassword(const std::string &ssid, const std::string &password);
void forget(const std::string &ssid);
std::vector<std::string> savedNetworks();

// Blocking helper with UI: connects if needed. Returns true when online.
bool ensureConnected();

// HTTP ----------------------------------------------------------------------

struct Response {
	long status = 0;
	std::string body;
	std::string contentType;
	std::string url; // final URL after redirects
	std::string error;
	bool truncated = false;
	bool verified = false; // HTTPS certificate verified
	bool certError = false;
};

// Return false to cancel the transfer
typedef bool (*ProgressFn)(void *user, size_t done, size_t total);

struct Request {
	std::string url;
	std::string postData;    // non-empty: POST (application/x-www-form-urlencoded)
	size_t maxBytes = 2 * 1024 * 1024;
	bool verifyTls = true;
	// Public, non-sensitive data (weather, box art): if the certificate can't
	// be verified, try again without verification instead of failing
	bool allowInsecureFallback = false;
	std::string outputFile;  // non-empty: write the body to this file
	ProgressFn progress = nullptr;
	void *user = nullptr;
};

bool fetch(const Request &req, Response &res);
// Convenience: GET with a "please wait" screen, cancellable with B
bool fetchWithUi(const std::string &title, Request req, Response &res);

std::string urlEncode(const std::string &s);

// NTP -----------------------------------------------------------------------
// Returns the current UTC time (seconds since 1970)
bool ntpTime(const std::string &server, time_t &utc, std::string &error);

} // namespace net

// dsi-dash — Wi-Fi dashboard for Nintendo DSi (DS-mode binary; TWL/DSi-mode
// gets 16MB RAM automatically when launched via nds-bootstrap in DSi mode).
// Toolchain: devkitARM + libnds 2.x (Calico) + dswifi 2.1
//
// Top screen:    network-synced clock + date + location + update age
// Bottom screen: paginated -> [Clima agora] [Previsao 3 dias] [Rede]
// A = refresh, L/R = trocar pagina, START = sair. Auto-refresh a cada 10 min.
#include <nds.h>
#include <dswifi9.h>
#include <calico/nds/pm.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEMP_UNKNOWN 9999.0
#define HTTP_BUF_SIZE (24 * 1024)
#define FC_DAYS 3
#define AUTO_REFRESH_SEC 600
#define PAGE_COUNT 3

static char g_status[96] = "Init...";
static char g_city[48] = "?";
static char g_region[48] = "";
static char g_ccode[8] = "";
static char g_tz[48] = "";
static char g_ip[20] = "";
static double g_lat = 9999.0, g_lon = 9999.0;
static double g_temp = TEMP_UNKNOWN;
static double g_wind = 9999.0;
static int g_hum = -1, g_code = -1;
static bool g_haveWeather = false;

// 3-day forecast
static double g_fcMax[FC_DAYS], g_fcMin[FC_DAYS];
static int g_fcCode[FC_DAYS];
static char g_fcDate[FC_DAYS][12];
static int g_fcCount = 0;

// network-synced clock
static long g_clockOffset = 0;   // add to time(NULL) to get correct local epoch
static int g_utcOffset = 0;      // utc_offset_seconds from open-meteo
static bool g_haveUtcOff = false;
static bool g_clockSynced = false;
static time_t g_dateEpochUtc = 0; // from HTTP Date: header
static time_t g_dateAtDevice = 0; // time(NULL) captured at same instant
static bool g_haveDate = false;

static time_t g_lastFetch = 0;
static int g_page = 0;

// ---------------------------------------------------------------------------
// synced-clock helpers
// ---------------------------------------------------------------------------
static time_t nowUnix(void) { return (time_t)((long)time(NULL) + g_clockOffset); }

// days since 1970-01-01 for a civil date (Howard Hinnant's algorithm)
static long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

// 0=Sun .. 6=Sat
static int weekdayFromCivil(int y, int m, int d) {
    long z = daysFromCivil(y, m, d);
    int wd = (int)(((z % 7) + 4) % 7);
    return wd < 0 ? wd + 7 : wd;
}

// parse "Www, DD Mon YYYY HH:MM:SS GMT" -> UTC epoch, or (time_t)-1
static time_t parseHttpDate(const char* s) {
    static const char* MON = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int day, year, hh, mm, ss;
    char mon[8] = {0};
    if (sscanf(s, "%*3s, %d %7s %d %d:%d:%d", &day, mon, &year, &hh, &mm, &ss) != 6)
        return (time_t)-1;
    const char* mp = strstr(MON, mon);
    if (!mp) return (time_t)-1;
    int month = (int)((mp - MON) / 3) + 1;
    long days = daysFromCivil(year, month, day);
    return (time_t)(days * 86400L + hh * 3600L + mm * 60L + ss);
}

static void syncClock(void) {
    if (g_haveDate && g_haveUtcOff) {
        time_t desiredLocal = g_dateEpochUtc + g_utcOffset;
        g_clockOffset = (long)desiredLocal - (long)g_dateAtDevice;
        g_clockSynced = true;
    }
}

// ---------------------------------------------------------------------------
// minimal JSON string/number extraction (flat docs)
// ---------------------------------------------------------------------------
static bool jsonFind(const char* json, const char* key, const char** valStart, size_t* valLen) {
    char pat[64];
    int n = snprintf(pat, sizeof(pat), "\"%s\":", key);
    if (n <= 0 || (size_t)n >= sizeof(pat)) return false;
    const char* p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    *valStart = p;
    *valLen = strspn(p, "0123456789+-.");
    return true;
}

static bool jsonStr(const char* json, const char* key, char* out, size_t outSize) {
    const char* v; size_t len;
    if (!jsonFind(json, key, &v, &len)) return false;
    if (*v != '"') return false;
    v++;
    const char* e = strchr(v, '"');
    if (!e) return false;
    len = e - v;
    if (len >= outSize) len = outSize - 1;
    memcpy(out, v, len);
    out[len] = 0;
    return true;
}

static bool jsonDouble(const char* json, const char* key, double* out) {
    const char* v; size_t len;
    if (!jsonFind(json, key, &v, &len)) return false;
    if (*v != '"' && len > 0 && len < 32) {
        char tmp[32];
        memcpy(tmp, v, len);
        tmp[len] = 0;
        *out = strtod(tmp, NULL);
        return true;
    }
    return false;
}

static bool jsonInt(const char* json, const char* key, int* out) {
    const char* v; size_t len;
    if (!jsonFind(json, key, &v, &len)) return false;
    if (*v != '"' && len > 0 && len < 16) {
        char tmp[16];
        memcpy(tmp, v, len);
        tmp[len] = 0;
        *out = (int)strtol(tmp, NULL, 10);
        return true;
    }
    return false;
}

// numeric array element: "key":[a,b,c]  -> element idx
static bool jsonArrNum(const char* block, const char* key, int idx, double* out) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":[", key);
    const char* p = strstr(block, pat);
    if (!p) return false;
    p += strlen(pat);
    const char* end = strchr(p, ']');
    if (!end) return false;
    for (int i = 0; i < idx; i++) {
        p = strchr(p, ',');
        if (!p || p > end) return false;
        p++;
    }
    *out = strtod(p, NULL);
    return true;
}

// string array element: "key":["a","b"] -> element idx
static bool jsonArrStr(const char* block, const char* key, int idx, char* out, size_t sz) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":[", key);
    const char* p = strstr(block, pat);
    if (!p) return false;
    p += strlen(pat);
    const char* end = strchr(p, ']');
    if (!end) return false;
    for (int i = 0; i < idx; i++) {
        p = strchr(p, ',');
        if (!p || p > end) return false;
        p++;
    }
    while (*p == ' ' || *p == '"') p++;
    const char* q = p;
    while (*q && *q != '"' && *q != ',' && *q != ']') q++;
    size_t l = q - p;
    if (l >= sz) l = sz - 1;
    memcpy(out, p, l);
    out[l] = 0;
    return true;
}

// ---------------------------------------------------------------------------
// HTTP GET (plain HTTP only — no TLS on this hardware).
// If dateOut != NULL, the response "Date:" header (UTC) is copied into it.
// ---------------------------------------------------------------------------
static int httpGet(const char* host, const char* path, char* body, size_t bodySize,
                   char* dateOut, size_t dateOutSz) {
    if (dateOut && dateOutSz) dateOut[0] = 0;
    struct hostent* h = gethostbyname((char*)host);
    if (!h || !h->h_addr_list[0]) return -2;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -3;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(80);
    memcpy(&sa.sin_addr.s_addr, h->h_addr_list[0], 4);
    if (connect(sock, (struct sockaddr*)&sa, sizeof(sa)) < 0) { closesocket(sock); return -4; }

    char req[512];
    int rl = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: dsi-dash/1.1\r\nAccept: */*\r\nConnection: close\r\n\r\n",
        path, host);
    if (rl <= 0 || send(sock, req, rl, 0) < 0) { closesocket(sock); return -5; }

    size_t total = 0;
    int n;
    while (total < bodySize - 1 && (n = recv(sock, body + total, (int)(bodySize - 1 - total), 0)) > 0) {
        total += n;
    }
    body[total] = 0;
    closesocket(sock);
    if (total == 0) return -6;

    char* b = strstr(body, "\r\n\r\n");
    if (!b) return -7;

    // capture Date: header from the header block only
    if (dateOut && dateOutSz) {
        char saved = *b;
        *b = 0;
        char* dh = strstr(body, "\r\nDate:");
        if (!dh && strncmp(body, "Date:", 5) == 0) dh = body - 2; // Date: as first header
        if (dh) {
            dh += (strncmp(dh, "\r\n", 2) == 0) ? 7 : 5;
            while (*dh == ' ') dh++;
            char* e = strstr(dh, "\r\n");
            size_t l = e ? (size_t)(e - dh) : strlen(dh);
            if (l >= dateOutSz) l = dateOutSz - 1;
            memcpy(dateOut, dh, l);
            dateOut[l] = 0;
        }
        *b = saved;
    }

    b += 4;
    memmove(body, b, total - (b - body) + 1);
    return (int)strlen(body);
}

// ---------------------------------------------------------------------------
// data fetch
// ---------------------------------------------------------------------------
static char g_httpBuf[HTTP_BUF_SIZE];

static void captureDate(const char* dateHdr) {
    if (!dateHdr || !dateHdr[0]) return;
    time_t utc = parseHttpDate(dateHdr);
    if (utc != (time_t)-1) {
        g_dateEpochUtc = utc;
        g_dateAtDevice = time(NULL);
        g_haveDate = true;
    }
}

static void fetchGeo(void) {
    char dateHdr[40];
    int n = httpGet("ip-api.com", "/json/?fields=status,country,countryCode,region,city,lat,lon,timezone,query",
                    g_httpBuf, sizeof(g_httpBuf), dateHdr, sizeof(dateHdr));
    if (n <= 0) { snprintf(g_status, sizeof(g_status), "ip-api erro (%d)", n); return; }
    captureDate(dateHdr);
    if (!jsonStr(g_httpBuf, "city", g_city, sizeof(g_city))) { strcpy(g_status, "geo parse err"); return; }
    jsonStr(g_httpBuf, "region", g_region, sizeof(g_region));
    jsonStr(g_httpBuf, "countryCode", g_ccode, sizeof(g_ccode));
    jsonStr(g_httpBuf, "timezone", g_tz, sizeof(g_tz));
    jsonStr(g_httpBuf, "query", g_ip, sizeof(g_ip));
    jsonDouble(g_httpBuf, "lat", &g_lat);
    jsonDouble(g_httpBuf, "lon", &g_lon);
    snprintf(g_status, sizeof(g_status), "Geo: %s (%s)", g_city, g_ccode);
}

static void fetchWeather(void) {
    if (g_lat > 9000.0 || g_lon > 9000.0) { strcpy(g_status, "sem coords"); return; }
    char path[256];
    // newlib soft-float printf handles %.3f on the ARM9 without FPU
    snprintf(path, sizeof(path),
        "/v1/forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m,"
        "relative_humidity_2m,wind_speed_10m,weather_code"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min"
        "&forecast_days=%d&timezone=auto",
        g_lat, g_lon, FC_DAYS);
    char dateHdr[40];
    int n = httpGet("api.open-meteo.com", path, g_httpBuf, sizeof(g_httpBuf), dateHdr, sizeof(dateHdr));
    if (n <= 0) { snprintf(g_status, sizeof(g_status), "open-meteo erro (%d)", n); return; }
    captureDate(dateHdr);

    // top-level utc offset (present once, with timezone=auto)
    if (jsonInt(g_httpBuf, "utc_offset_seconds", &g_utcOffset)) g_haveUtcOff = true;

    // current block (duplicate keys elsewhere, so scope the search)
    const char* cur = strstr(g_httpBuf, "\"current\":");
    if (!cur) { strcpy(g_status, "weather parse err"); return; }
    jsonDouble(cur, "temperature_2m", &g_temp);
    jsonDouble(cur, "wind_speed_10m", &g_wind);
    jsonInt(cur, "relative_humidity_2m", &g_hum);
    jsonInt(cur, "weather_code", &g_code);
    g_haveWeather = true;

    // daily block -> 3-day forecast
    const char* daily = strstr(g_httpBuf, "\"daily\":");
    g_fcCount = 0;
    if (daily) {
        for (int i = 0; i < FC_DAYS; i++) {
            double mx, mn, cd;
            if (!jsonArrStr(daily, "time", i, g_fcDate[i], sizeof(g_fcDate[i]))) break;
            if (!jsonArrNum(daily, "temperature_2m_max", i, &mx)) break;
            if (!jsonArrNum(daily, "temperature_2m_min", i, &mn)) break;
            jsonArrNum(daily, "weather_code", i, &cd);
            g_fcMax[i] = mx; g_fcMin[i] = mn; g_fcCode[i] = (int)cd;
            g_fcCount++;
        }
    }
    snprintf(g_status, sizeof(g_status), "Clima OK (%d bytes)", n);
}

// wait until associated (up to ~5s); reconnect if the link dropped
static void ensureLink(void) {
    if (Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED) return;
    Wifi_AutoConnect();
    for (int i = 0; i < 300; i++) {
        if (Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED) return;
        swiWaitForVBlank();
    }
}

static void refreshAll(void) {
    ensureLink();
    fetchGeo();
    fetchWeather();
    syncClock();
    g_lastFetch = nowUnix();
}

// ---------------------------------------------------------------------------
// weather code -> text (open-meteo WMO codes)
// ---------------------------------------------------------------------------
static const char* wmoText(int c) {
    switch (c) {
        case 0:  return "Ceu limpo";
        case 1: case 2: return "Parcialm. nublado";
        case 3:  return "Nublado";
        case 45: case 48: return "Nevoeiro";
        case 51: case 53: case 55: return "Garoa";
        case 56: case 57: return "Garoa congelante";
        case 61: case 63: case 65: return "Chuva";
        case 66: case 67: return "Chuva congelante";
        case 71: case 73: case 75: return "Neve";
        case 77: return "Graos de neve";
        case 80: case 81: case 82: return "Pancadas";
        case 85: case 86: return "Pancadas de neve";
        case 95: return "Trovoada";
        case 96: case 99: return "Trovoada c/ granizo";
        default: return "?";
    }
}

// short label for the narrow forecast rows
static const char* wmoShort(int c) {
    switch (c) {
        case 0:  return "Limpo";
        case 1: case 2: return "P.nublado";
        case 3:  return "Nublado";
        case 45: case 48: return "Nevoeiro";
        case 51: case 53: case 55: return "Garoa";
        case 56: case 57: return "Garoa cong.";
        case 61: case 63: case 65: return "Chuva";
        case 66: case 67: return "Chuva cong.";
        case 71: case 73: case 75: return "Neve";
        case 77: return "Neve";
        case 80: case 81: case 82: return "Pancadas";
        case 85: case 86: return "Panc. neve";
        case 95: return "Trovoada";
        case 96: case 99: return "Trov+granizo";
        default: return "?";
    }
}

static const char* dowName(int wd) {
    static const char* N[] = {"Dom","Seg","Ter","Qua","Qui","Sex","Sab"};
    return (wd >= 0 && wd < 7) ? N[wd] : "?";
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
static PrintConsole topConsole, botConsole;

static void fmtTemp1(char* out, size_t sz, double v) {
    int t10 = (int)(v * 10.0);
    int sign = (t10 < 0);
    if (sign) t10 = -t10;
    snprintf(out, sz, "%s%d.%d", sign ? "-" : "", t10 / 10, t10 % 10);
}

static void drawTop(void) {
    consoleSelect(&topConsole);
    consoleClear();
    time_t t = nowUnix();
    struct tm* lt = g_clockSynced ? gmtime(&t) : localtime(&t);
    char clockS[16], dateS[40];
    strftime(clockS, sizeof(clockS), "%H:%M:%S", lt);
    strftime(dateS, sizeof(dateS), "%a %d %b %Y", lt);
    int x = (32 - (int)strlen(clockS)) / 2;
    iprintf("\x1b[%d;%dH%s", 3, x, clockS);
    x = (32 - (int)strlen(dateS)) / 2;
    iprintf("\x1b[%d;%dH%s", 5, x, dateS);
    iprintf("\x1b[%d;%dH%s", 7, 1, g_clockSynced ? "* relogio sincronizado (net)" : "  relogio: RTC local");

    iprintf("\x1b[%d;%dHDSi Dash v1.1", 9, 1);
    if (g_city[0] && g_city[0] != '?')
        iprintf("\x1b[%d;%dHLocal: %s %s", 10, 1, g_city, g_region);
    if (g_tz[0]) iprintf("\x1b[%d;%dHTZ: %s", 11, 1, g_tz);
    if (g_ip[0]) iprintf("\x1b[%d;%dHIP: %s", 12, 1, g_ip);

    if (g_lastFetch) {
        long age = (long)nowUnix() - (long)g_lastFetch;
        if (age < 0) age = 0;
        iprintf("\x1b[%d;%dHAtualizado ha %lds", 14, 1, age);
    }
}

static void drawPageWeather(void) {
    iprintf("\x1b[0;0H=== CLIMA AGORA ===");
    if (g_haveWeather) {
        char tempS[16], windS[16];
        fmtTemp1(tempS, sizeof(tempS), g_temp);
        fmtTemp1(windS, sizeof(windS), g_wind);
        iprintf("\x1b[2;1H%s C em %s", tempS, g_city);
        iprintf("\x1b[3;1H%s", wmoText(g_code));
        iprintf("\x1b[4;1HUmidade: %d%%", g_hum);
        iprintf("\x1b[5;1HVento: %s km/h", windS);
    } else {
        iprintf("\x1b[2;1HClima: -- (aperte A)");
    }
}

static void drawPageForecast(void) {
    iprintf("\x1b[0;0H=== PREVISAO %d DIAS ===", g_fcCount);
    if (g_fcCount == 0) {
        iprintf("\x1b[2;1HSem dados (aperte A)");
        return;
    }
    for (int i = 0; i < g_fcCount; i++) {
        int y, m, d;
        int wd = -1;
        if (sscanf(g_fcDate[i], "%d-%d-%d", &y, &m, &d) == 3)
            wd = weekdayFromCivil(y, m, d);
        char mxS[16], mnS[16];
        fmtTemp1(mxS, sizeof(mxS), g_fcMax[i]);
        fmtTemp1(mnS, sizeof(mnS), g_fcMin[i]);
        int row = 2 + i * 3;
        iprintf("\x1b[%d;1H%s %02d/%02d  %s/%s C", row, dowName(wd), d, m, mnS, mxS);
        iprintf("\x1b[%d;1H  %s", row + 1, wmoShort(g_fcCode[i]));
    }
}

static const char* assocText(int s) {
    switch (s) {
        case ASSOCSTATUS_DISCONNECTED:  return "desconectado";
        case ASSOCSTATUS_SEARCHING:     return "procurando AP";
        case ASSOCSTATUS_ASSOCIATING:   return "associando";
        case ASSOCSTATUS_ACQUIRINGDHCP: return "DHCP...";
        case ASSOCSTATUS_ASSOCIATED:    return "conectado";
        default: return "?";
    }
}

static void drawPageNet(void) {
    iprintf("\x1b[0;0H=== REDE ===");
    iprintf("\x1b[2;1HWi-Fi: %s", assocText(Wifi_AssocStatus()));
    struct in_addr ip, gw, mask, dns1, dns2;
    ip = Wifi_GetIPInfo(&gw, &mask, &dns1, &dns2);
    iprintf("\x1b[4;1HmeuIP: %s", inet_ntoa(ip));
    iprintf("\x1b[5;1HgtwIP: %s", inet_ntoa(gw));
    iprintf("\x1b[6;1Hmask : %s", inet_ntoa(mask));
    iprintf("\x1b[7;1HDNS1 : %s", inet_ntoa(dns1));
    iprintf("\x1b[8;1HDNS2 : %s", inet_ntoa(dns2));
    iprintf("\x1b[10;1HEstado: %s", g_status);
}

static void drawBottom(void) {
    consoleSelect(&botConsole);
    consoleClear();
    switch (g_page) {
        case 0: drawPageWeather(); break;
        case 1: drawPageForecast(); break;
        case 2: drawPageNet(); break;
    }
    iprintf("\x1b[16;0H pag %d/%d  L/R muda", g_page + 1, PAGE_COUNT);
    iprintf("\x1b[17;0H[A]atualizar [START]sair");
}

// ---------------------------------------------------------------------------
int main(void) {
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    // libnds expands its 1bpp default font to 4bpp tiles; an 8bpp BG renders garbage
    consoleInit(&topConsole, 0, BgType_Text4bpp, BgSize_T_256x256, 22, 3, true, true);
    consoleInit(&botConsole, 0, BgType_Text4bpp, BgSize_T_256x256, 22, 3, false, true);

    consoleSelect(&topConsole);
    iprintf("\x1b[8;1HInicializando Wi-Fi...");
    bool ok = Wifi_InitDefault(WFC_CONNECT);
    if (!ok) {
        iprintf("\x1b[9;1HWi-Fi falhou (sem perfil WFC)");
        iprintf("\x1b[11;1HConfigure conexao no menu do DSi");
        while (pmMainLoop()) {
            swiWaitForVBlank();
            scanKeys();
            if (keysDown() & KEY_START) return 0;
        }
        return 0;
    }
    iprintf("\x1b[9;1HWi-Fi conectado!");
    refreshAll();

    time_t lastClockDraw = 0;
    bool needRedraw = true;
    while (pmMainLoop()) {
        swiWaitForVBlank();
        scanKeys();
        u32 keys = keysDown();

        if (keys & KEY_A) {
            refreshAll();
            needRedraw = true;
        }
        if (keys & KEY_R) { g_page = (g_page + 1) % PAGE_COUNT; needRedraw = true; }
        if (keys & KEY_L) { g_page = (g_page + PAGE_COUNT - 1) % PAGE_COUNT; needRedraw = true; }
        if (keys & KEY_START) break;

        // auto-refresh
        if (g_lastFetch && (long)nowUnix() - (long)g_lastFetch >= AUTO_REFRESH_SEC) {
            refreshAll();
            needRedraw = true;
        }

        time_t now = nowUnix();
        if (now != lastClockDraw) {
            drawTop();
            lastClockDraw = now;
            needRedraw = true;
        }
        if (needRedraw) {
            drawBottom();
            needRedraw = false;
        }
    }
    return 0;
}

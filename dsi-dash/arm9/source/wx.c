// DSi Dash — dados de clima (ip-api.com + open-meteo.com, ambos HTTP)
#include "common.h"

Weather g_wx;
static Weather s_new;
static NetJob s_job;
static char s_err[96];
static int s_retry;

#define AUTO_REFRESH_SEC (15 * 60)

static int t10(double v) { return (int)(v * 10.0 + (v >= 0 ? 0.5 : -0.5)); }

static void wxJob(NetJob* j) {
	Weather* w = &s_new;
	memset(w, 0, sizeof(*w));
	HttpResp r;
	// 1) localizacao
	if (g_set.manualLoc && g_set.lat[0] && g_set.lon[0]) {
		snprintf(w->city, sizeof(w->city), "%s", g_set.city);
		snprintf(w->lat, sizeof(w->lat), "%s", g_set.lat);
		snprintf(w->lon, sizeof(w->lon), "%s", g_set.lon);
		if (httpGet(j, "http://ip-api.com/json/?fields=status,query", &r, 4096) == 0) {
			jsonStr(r.body, "query", w->ip, sizeof(w->ip));
			httpFree(&r);
		}
	} else {
		if (httpGet(j, "http://ip-api.com/json/?fields=status,countryCode,region,regionName,city,lat,lon,timezone,query&lang=pt-BR", &r, 8192) < 0) {
			j->result = -1;
			return;
		}
		double lat = 0, lon = 0;
		if (!jsonStr(r.body, "city", w->city, sizeof(w->city)) || !jsonDouble(r.body, "lat", &lat) || !jsonDouble(r.body, "lon", &lon)) {
			snprintf(j->err, sizeof(j->err), "Localiza\xC3\xA7\xC3\xA3o indispon\xC3\xADvel");
			httpFree(&r);
			j->result = -2;
			return;
		}
		jsonStr(r.body, "region", w->region, sizeof(w->region));
		jsonStr(r.body, "countryCode", w->cc, sizeof(w->cc));
		jsonStr(r.body, "timezone", w->tz, sizeof(w->tz));
		jsonStr(r.body, "query", w->ip, sizeof(w->ip));
		snprintf(w->lat, sizeof(w->lat), "%.3f", lat);
		snprintf(w->lon, sizeof(w->lon), "%.3f", lon);
		httpFree(&r);
	}
	// 2) clima
	char* url = (char*)malloc(768);
	if (!url) return;
	snprintf(url, 768,
		"http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
		"&current=temperature_2m,apparent_temperature,relative_humidity_2m,wind_speed_10m,weather_code,is_day,precipitation"
		"&hourly=temperature_2m,precipitation_probability,weather_code,is_day"
		"&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
		"&forecast_days=%d&forecast_hours=%d&timezone=auto",
		w->lat, w->lon, WX_DAYS, WX_HOURS);
	int rc = httpGet(j, url, &r, 32768);
	free(url);
	if (rc < 0) {
		j->result = -3;
		return;
	}
	const char* b = r.body;
	int off = 0;
	if (jsonInt(b, "utc_offset_seconds", &off)) sysSetUtcOffset(off);
	if (!w->tz[0]) jsonStr(b, "timezone", w->tz, sizeof(w->tz));
	const char* cur = strstr(b, "\"current\":{");
	if (!cur) {
		snprintf(j->err, sizeof(j->err), "Resposta de clima inv\xC3\xA1lida");
		httpFree(&r);
		j->result = -4;
		return;
	}
	double v;
	if (jsonDouble(cur, "temperature_2m", &v)) w->temp10 = t10(v);
	if (jsonDouble(cur, "apparent_temperature", &v)) w->feels10 = t10(v);
	if (jsonDouble(cur, "wind_speed_10m", &v)) w->wind10 = t10(v);
	if (jsonDouble(cur, "precipitation", &v)) w->precip10 = t10(v);
	jsonInt(cur, "relative_humidity_2m", &w->hum);
	jsonInt(cur, "weather_code", &w->code);
	jsonInt(cur, "is_day", &w->isDay);

	const char* hr = strstr(b, "\"hourly\":{");
	if (hr) {
		for (int i = 0; i < WX_HOURS; i++) {
			char ts[20];
			double tt, pp, cc, dd;
			if (!jsonArrStr(hr, "time", i, ts, sizeof(ts)) || !jsonArrNum(hr, "temperature_2m", i, &tt)) break;
			w->hTemp10[i] = t10(tt);
			w->hPop[i] = jsonArrNum(hr, "precipitation_probability", i, &pp) ? (int)pp : 0;
			w->hCode[i] = jsonArrNum(hr, "weather_code", i, &cc) ? (int)cc : 0;
			w->hDay[i] = jsonArrNum(hr, "is_day", i, &dd) ? (int)dd : 1;
			w->hHour[i] = (strlen(ts) >= 13) ? atoi(ts + 11) : 0;
			w->hCount = i + 1;
		}
	}
	const char* dy = strstr(b, "\"daily\":{");
	if (dy) {
		for (int i = 0; i < WX_DAYS; i++) {
			char ds[16], sr[24];
			double mx, mn, cc, pp;
			if (!jsonArrStr(dy, "time", i, ds, sizeof(ds))) break;
			if (!jsonArrNum(dy, "temperature_2m_max", i, &mx) || !jsonArrNum(dy, "temperature_2m_min", i, &mn)) break;
			w->dMax10[i] = t10(mx);
			w->dMin10[i] = t10(mn);
			w->dCode[i] = jsonArrNum(dy, "weather_code", i, &cc) ? (int)cc : 0;
			w->dPop[i] = jsonArrNum(dy, "precipitation_probability_max", i, &pp) ? (int)pp : 0;
			sscanf(ds, "%d-%d-%d", &w->dY[i], &w->dM[i], &w->dD[i]);
			if (i == 0) {
				if (jsonArrStr(dy, "sunrise", 0, sr, sizeof(sr)) && strlen(sr) >= 16) memcpy(w->sunrise, sr + 11, 5);
				if (jsonArrStr(dy, "sunset", 0, sr, sizeof(sr)) && strlen(sr) >= 16) memcpy(w->sunset, sr + 11, 5);
			}
			w->dCount = i + 1;
		}
	}
	httpFree(&r);
	w->valid = true;
	j->result = 0;
}

static void saveCache(void) {
	if (!sysHasStorage()) return;
	char p[96];
	sysDataPath(p, sizeof(p), "weather.bin");
	FILE* f = fopen(p, "wb");
	if (!f) return;
	u32 magic = 0x57583032;  // 'WX02'
	fwrite(&magic, 4, 1, f);
	fwrite(&g_wx, sizeof(g_wx), 1, f);
	fclose(f);
}

static void loadCache(void) {
	if (!sysHasStorage()) return;
	char p[96];
	sysDataPath(p, sizeof(p), "weather.bin");
	FILE* f = fopen(p, "rb");
	if (!f) return;
	u32 magic = 0;
	Weather w;
	if (fread(&magic, 4, 1, f) == 1 && magic == 0x57583032 && fread(&w, sizeof(w), 1, f) == 1) {
		g_wx = w;
	}
	fclose(f);
}

void wxInit(void) {
	loadCache();
	wxRefresh();
}

void wxRefresh(void) {
	if (jobBusy(&s_job)) return;
	s_job.run = wxJob;
	s_err[0] = 0;
	netSubmit(&s_job);
	gfxInvalidate(GFX_BOTH);
}

bool wxBusy(void) { return jobBusy(&s_job); }
const char* wxError(void) { return s_err; }

void wxTick(void) {
	if (s_job.state == JOB_DONE) {
		s_job.state = JOB_IDLE;
		if (s_job.result == 0 && s_new.valid) {
			s_new.fetched = sysNow();
			g_wx = s_new;
			s_err[0] = 0;
			s_retry = 0;
			saveCache();
		} else {
			snprintf(s_err, sizeof(s_err), "%s", s_job.err[0] ? s_job.err : "Falha ao buscar clima");
			s_retry = 60 * 60;  // tenta de novo em ~1 min
		}
		gfxInvalidate(GFX_BOTH);
	}
	if (s_retry > 0 && --s_retry == 0) wxRefresh();
	// atualizacao automatica
	if (!jobBusy(&s_job) && g_wx.valid && netState() == NET_ONLINE && (long)(sysNow() - g_wx.fetched) > AUTO_REFRESH_SEC) wxRefresh();
}

// ---------------------------------------------------------------------------
// textos / icones
// ---------------------------------------------------------------------------
const char* wmoText(int c) {
	switch (c) {
		case 0: return "C\xC3\xA9u limpo";
		case 1: return "Predominantemente limpo";
		case 2: return "Parcialmente nublado";
		case 3: return "Nublado";
		case 45: case 48: return "Nevoeiro";
		case 51: case 53: case 55: return "Garoa";
		case 56: case 57: return "Garoa congelante";
		case 61: return "Chuva fraca";
		case 63: return "Chuva";
		case 65: return "Chuva forte";
		case 66: case 67: return "Chuva congelante";
		case 71: case 73: case 75: return "Neve";
		case 77: return "Gr\xC3\xA3os de neve";
		case 80: case 81: return "Pancadas de chuva";
		case 82: return "Pancadas fortes";
		case 85: case 86: return "Pancadas de neve";
		case 95: return "Trovoada";
		case 96: case 99: return "Trovoada com granizo";
	}
	return "\xE2\x80\x94";
}

const char* wmoShort(int c) {
	switch (c) {
		case 0: return "Limpo";
		case 1: case 2: return "Parc. nublado";
		case 3: return "Nublado";
		case 45: case 48: return "Nevoeiro";
		case 51: case 53: case 55: case 56: case 57: return "Garoa";
		case 61: case 63: case 65: case 66: case 67: return "Chuva";
		case 71: case 73: case 75: case 77: return "Neve";
		case 80: case 81: case 82: return "Pancadas";
		case 85: case 86: return "Neve";
		case 95: case 96: case 99: return "Trovoada";
	}
	return "\xE2\x80\x94";
}

void fmtTemp(char* out, int sz, int t) {
	int r = (t >= 0 ? t + 5 : t - 5) / 10;
	snprintf(out, sz, "%d\xC2\xB0", r);
}

enum { WX_CLEAR, WX_PARTLY, WX_CLOUDY, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM };

static int wxKind(int c) {
	if (c == 0) return WX_CLEAR;
	if (c == 1 || c == 2) return WX_PARTLY;
	if (c == 3) return WX_CLOUDY;
	if (c == 45 || c == 48) return WX_FOG;
	if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WX_SNOW;
	if (c >= 95) return WX_STORM;
	if (c >= 51) return WX_RAIN;
	return WX_CLOUDY;
}

void wxDrawIcon(Canvas* c, int code, bool day, int x, int y, bool big, bool onLight) {
	int S = big ? 48 : 24;
	int sun = big ? IC_WX_SUN_48 : IC_WX_SUN_24;
	int moon = big ? IC_WX_MOON_48 : IC_WX_MOON_24;
	int cloud = big ? IC_WX_CLOUD_48 : IC_WX_CLOUD_24;
	int rain = big ? IC_WX_RAIN_48 : IC_WX_RAIN_24;
	int snow = big ? IC_WX_SNOW_48 : IC_WX_SNOW_24;
	int bolt = big ? IC_WX_BOLT_48 : IC_WX_BOLT_24;
	int fog = big ? IC_WX_FOG_48 : IC_WX_FOG_24;
	u16 sunC = HEX(0xFFC83D), moonC = HEX(0xF3E7B3);
	u16 cloudC = onLight ? HEX(0xAEBDC6) : HEX(0xF2F5F7);
	u16 cloud2 = onLight ? HEX(0x8FA3AF) : HEX(0xCBD5DB);
	switch (wxKind(code)) {
		case WX_CLEAR:
			cvIcon(c, day ? sun : moon, x, y, day ? sunC : moonC);
			break;
		case WX_PARTLY:
			cvIcon(c, day ? sun : moon, x - S / 7, y - S / 7, day ? sunC : moonC);
			cvIcon(c, cloud, x + S / 10, y + S / 8, cloudC);
			break;
		case WX_CLOUDY:
			cvIcon(c, cloud, x - S / 8, y - S / 8, cloud2);
			cvIcon(c, cloud, x + S / 10, y + S / 12, cloudC);
			break;
		case WX_FOG:
			cvIcon(c, fog, x, y, cloudC);
			break;
		case WX_RAIN:
			cvIcon(c, rain, x, y + S / 4, HEX(0x4FC3F7));
			cvIcon(c, cloud, x, y - S / 6, cloudC);
			break;
		case WX_SNOW:
			cvIcon(c, snow, x, y + S / 4, onLight ? HEX(0x81D4FA) : HEX(0xFFFFFF));
			cvIcon(c, cloud, x, y - S / 6, cloudC);
			break;
		case WX_STORM:
			cvIcon(c, bolt, x, y + S / 5, HEX(0xFFD54F));
			cvIcon(c, cloud, x, y - S / 6, cloud2);
			break;
	}
}

void wxSkyColors(int code, bool day, u16* top, u16* bottom) {
	int k = wxKind(code);
	if (!day) {
		*top = HEX(0x0F2A44);
		*bottom = HEX(0x2C5374);
		if (k >= WX_CLOUDY) {
			*top = HEX(0x1F2A36);
			*bottom = HEX(0x3E4C5A);
		}
		return;
	}
	switch (k) {
		case WX_CLEAR: *top = HEX(0x2E8BEA); *bottom = HEX(0x7CC8FB); break;
		case WX_PARTLY: *top = HEX(0x3C8DD8); *bottom = HEX(0x8DB9DD); break;
		case WX_CLOUDY:
		case WX_FOG: *top = HEX(0x5F7485); *bottom = HEX(0x98A9B6); break;
		case WX_RAIN:
		case WX_SNOW: *top = HEX(0x4A5A6A); *bottom = HEX(0x7C8C99); break;
		default: *top = HEX(0x343F4B); *bottom = HEX(0x5E6A75); break;
	}
}

// DSi Dash — extracao minima de JSON (documentos pequenos e conhecidos)
#include "common.h"

const char* jsonSkipTo(const char* json, const char* key) {
	char pat[80];
	int n = snprintf(pat, sizeof(pat), "\"%s\":", key);
	if (n <= 0 || n >= (int)sizeof(pat)) return NULL;
	const char* p = strstr(json, pat);
	if (!p) return NULL;
	p += n;
	while (*p == ' ') p++;
	return p;
}

static void putUtf8(char** o, char* end, u32 c) {
	char* p = *o;
	if (c < 0x80) {
		if (p + 1 > end) return;
		*p++ = c;
	} else if (c < 0x800) {
		if (p + 2 > end) return;
		*p++ = 0xC0 | (c >> 6);
		*p++ = 0x80 | (c & 0x3F);
	} else {
		if (p + 3 > end) return;
		*p++ = 0xE0 | (c >> 12);
		*p++ = 0x80 | ((c >> 6) & 0x3F);
		*p++ = 0x80 | (c & 0x3F);
	}
	*o = p;
}

int jsonUnescape(const char* in, int inLen, char* out, int outSz) {
	char* o = out;
	char* end = out + outSz - 1;
	const char* e = in + inLen;
	while (in < e && o < end) {
		if (*in != '\\') {
			*o++ = *in++;
			continue;
		}
		in++;
		if (in >= e) break;
		char c = *in++;
		switch (c) {
			case 'n': *o++ = '\n'; break;
			case 't': *o++ = ' '; break;
			case 'r': break;
			case 'u': {
				if (e - in < 4) break;
				char hx[5] = {in[0], in[1], in[2], in[3], 0};
				u32 cp = strtoul(hx, NULL, 16);
				in += 4;
				// par substituto -> ignora (fora do nosso conjunto de glifos)
				if (cp >= 0xD800 && cp <= 0xDFFF) {
					if (cp < 0xDC00 && e - in >= 6 && in[0] == '\\' && in[1] == 'u') in += 6;
					cp = '?';
				}
				putUtf8(&o, end, cp);
				break;
			}
			default: *o++ = c; break;
		}
	}
	*o = 0;
	return (int)(o - out);
}

bool jsonStr(const char* json, const char* key, char* out, int outSize) {
	const char* v = jsonSkipTo(json, key);
	if (!v || *v != '"') return false;
	v++;
	const char* e = v;
	while (*e && *e != '"') {
		if (*e == '\\' && e[1]) e++;
		e++;
	}
	jsonUnescape(v, (int)(e - v), out, outSize);
	return true;
}

bool jsonDouble(const char* json, const char* key, double* out) {
	const char* v = jsonSkipTo(json, key);
	if (!v || *v == '"' || *v == 'n') return false;
	*out = strtod(v, NULL);
	return true;
}

bool jsonInt(const char* json, const char* key, int* out) {
	const char* v = jsonSkipTo(json, key);
	if (!v || *v == '"' || *v == 'n') return false;
	*out = (int)strtol(v, NULL, 10);
	return true;
}

static const char* arrElem(const char* block, const char* key, int idx) {
	char pat[80];
	snprintf(pat, sizeof(pat), "\"%s\":[", key);
	const char* p = strstr(block, pat);
	if (!p) return NULL;
	p += strlen(pat);
	const char* end = strchr(p, ']');
	if (!end) return NULL;
	for (int i = 0; i < idx; i++) {
		p = strchr(p, ',');
		if (!p || p > end) return NULL;
		p++;
	}
	return p;
}

bool jsonArrNum(const char* block, const char* key, int idx, double* out) {
	const char* p = arrElem(block, key, idx);
	if (!p || *p == 'n' || *p == ']') return false;
	*out = strtod(p, NULL);
	return true;
}

bool jsonArrStr(const char* block, const char* key, int idx, char* out, int sz) {
	const char* p = arrElem(block, key, idx);
	if (!p) return false;
	while (*p == ' ' || *p == '"') p++;
	const char* q = p;
	while (*q && *q != '"' && *q != ',' && *q != ']') q++;
	int l = MIN((int)(q - p), sz - 1);
	memcpy(out, p, l);
	out[l] = 0;
	return true;
}

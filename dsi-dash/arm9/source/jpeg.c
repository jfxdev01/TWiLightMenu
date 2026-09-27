// DSi Dash — codificador JPEG baseline (4:2:2) em aritmetica inteira.
// Entrada: YUV422 da camera (bytes Y0 Cb Y1 Cr), largura multipla de 16 e altura de 8.
#include "common.h"
#include "jpeg.h"

static const u8 ZIGZAG[64] = {
	0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static const u8 QL[64] = {
	16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
	18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99,
};
static const u8 QC[64] = {
	17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};

static const u8 DC_L_BITS[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const u8 DC_C_BITS[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const u8 DC_VALS[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const u8 AC_L_BITS[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
static const u8 AC_L_VALS[162] = {
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
	0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};
static const u8 AC_C_BITS[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
static const u8 AC_C_VALS[162] = {
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
	0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
	0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};

// T[u][x] = C(u)/2 * cos((2x+1)u*pi/16) * 4096
static const s16 DCT_T[8][8] = {
	{1448, 1448, 1448, 1448, 1448, 1448, 1448, 1448},
	{2009, 1703, 1138, 400, -400, -1138, -1703, -2009},
	{1892, 784, -784, -1892, -1892, -784, 784, 1892},
	{1703, -400, -2009, -1138, 1138, 2009, 400, -1703},
	{1448, -1448, -1448, 1448, 1448, -1448, -1448, 1448},
	{1138, -2009, 400, 1703, -1703, -400, 2009, -1138},
	{784, -1892, 1892, -784, -784, 1892, -1892, 784},
	{400, -1138, 1703, -2009, 2009, -1703, 1138, -400},
};

typedef struct Huff {
	u16 code[256];
	u8 size[256];
} Huff;

typedef struct Enc {
	u8* out;
	int len, cap;
	u32 bits;
	int nbits;
	bool fail;
	Huff dcL, dcC, acL, acC;
	u8 qL[64], qC[64];     // em ordem natural
	u32 rL[64], rC[64];    // reciprocos (16.16)
} Enc;

static void put(Enc* e, u8 b) {
	if (e->len >= e->cap) {
		int nc = e->cap * 2;
		u8* n = (u8*)realloc(e->out, nc);
		if (!n) {
			e->fail = true;
			return;
		}
		e->out = n;
		e->cap = nc;
	}
	e->out[e->len++] = b;
}

static void put16(Enc* e, u16 v) {
	put(e, v >> 8);
	put(e, v & 0xFF);
}

static void putBits(Enc* e, u32 code, int size) {
	e->bits = (e->bits << size) | (code & ((1u << size) - 1));
	e->nbits += size;
	while (e->nbits >= 8) {
		u8 b = (e->bits >> (e->nbits - 8)) & 0xFF;
		put(e, b);
		if (b == 0xFF) put(e, 0);
		e->nbits -= 8;
	}
}

static void buildHuff(Huff* h, const u8* bits, const u8* vals) {
	int code = 0, k = 0;
	for (int l = 1; l <= 16; l++) {
		for (int i = 0; i < bits[l - 1]; i++) {
			h->code[vals[k]] = code;
			h->size[vals[k]] = l;
			k++;
			code++;
		}
		code <<= 1;
	}
}

static void writeDHT(Enc* e, int cls, int id, const u8* bits, const u8* vals, int nvals) {
	put16(e, 0xFFC4);
	put16(e, 2 + 1 + 16 + nvals);
	put(e, (cls << 4) | id);
	for (int i = 0; i < 16; i++) put(e, bits[i]);
	for (int i = 0; i < nvals; i++) put(e, vals[i]);
}

static void scaleQ(const u8* base, int quality, u8* q, u32* r) {
	quality = CLAMP(quality, 1, 100);
	int s = quality < 50 ? 5000 / quality : 200 - quality * 2;
	for (int i = 0; i < 64; i++) {
		int v = (base[i] * s + 50) / 100;
		v = CLAMP(v, 1, 255);
		q[i] = v;
		r[i] = (65536 + v / 2) / v;
	}
}

static int bitLen(int v) {
	if (v < 0) v = -v;
	int n = 0;
	while (v) {
		n++;
		v >>= 1;
	}
	return n;
}

// bloco 8x8 de amostras (0..255): DCT, quantizacao, Huffman
static int encodeBlock(Enc* e, const s16* px, const u32* recip, const Huff* dc, const Huff* ac, int prevDC) {
	int tmp[64];
	s16 coef[64];
	// linhas
	for (int y = 0; y < 8; y++) {
		const s16* row = px + y * 8;
		for (int u = 0; u < 8; u++) {
			int s = 0;
			for (int x = 0; x < 8; x++) s += row[x] * DCT_T[u][x];
			tmp[y * 8 + u] = s >> 10;
		}
	}
	// colunas
	for (int u = 0; u < 8; u++) {
		for (int v = 0; v < 8; v++) {
			int s = 0;
			for (int y = 0; y < 8; y++) s += tmp[y * 8 + u] * DCT_T[v][y];
			coef[v * 8 + u] = (s + (1 << 13)) >> 14;
		}
	}
	// quantiza em ordem zigue-zague
	int q[64];
	for (int i = 0; i < 64; i++) {
		int c = coef[ZIGZAG[i]];
		u32 r = recip[ZIGZAG[i]];
		q[i] = c >= 0 ? (int)((c * r + 32768) >> 16) : -(int)(((-c) * r + 32768) >> 16);
	}
	int diff = q[0] - prevDC;
	int n = bitLen(diff);
	putBits(e, dc->code[n], dc->size[n]);
	if (n) putBits(e, diff < 0 ? diff - 1 : diff, n);
	int run = 0;
	for (int i = 1; i < 64; i++) {
		if (!q[i]) {
			run++;
			continue;
		}
		while (run > 15) {
			putBits(e, ac->code[0xF0], ac->size[0xF0]);
			run -= 16;
		}
		n = bitLen(q[i]);
		int sym = (run << 4) | n;
		putBits(e, ac->code[sym], ac->size[sym]);
		putBits(e, q[i] < 0 ? q[i] - 1 : q[i], n);
		run = 0;
	}
	if (run) putBits(e, ac->code[0x00], ac->size[0x00]);
	return q[0];
}

int jpegEncodeYuv422(const u8* yuv, int w, int h, int quality, u8** outp) {
	Enc* e = (Enc*)calloc(1, sizeof(Enc));
	if (!e) return -1;
	e->cap = 128 * 1024;
	e->out = (u8*)malloc(e->cap);
	if (!e->out) {
		free(e);
		return -1;
	}
	scaleQ(QL, quality, e->qL, e->rL);
	scaleQ(QC, quality, e->qC, e->rC);
	buildHuff(&e->dcL, DC_L_BITS, DC_VALS);
	buildHuff(&e->dcC, DC_C_BITS, DC_VALS);
	buildHuff(&e->acL, AC_L_BITS, AC_L_VALS);
	buildHuff(&e->acC, AC_C_BITS, AC_C_VALS);

	// cabecalhos
	put16(e, 0xFFD8);
	static const u8 jfif[] = {0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
	for (int i = 0; i < (int)sizeof(jfif); i++) put(e, jfif[i]);
	put16(e, 0xFFDB);
	put16(e, 2 + 2 * 65);
	put(e, 0);
	for (int i = 0; i < 64; i++) put(e, e->qL[ZIGZAG[i]]);
	put(e, 1);
	for (int i = 0; i < 64; i++) put(e, e->qC[ZIGZAG[i]]);
	put16(e, 0xFFC0);
	put16(e, 17);
	put(e, 8);
	put16(e, h);
	put16(e, w);
	put(e, 3);
	put(e, 1), put(e, 0x21), put(e, 0);  // Y: 2x1
	put(e, 2), put(e, 0x11), put(e, 1);  // Cb
	put(e, 3), put(e, 0x11), put(e, 1);  // Cr
	writeDHT(e, 0, 0, DC_L_BITS, DC_VALS, 12);
	writeDHT(e, 1, 0, AC_L_BITS, AC_L_VALS, 162);
	writeDHT(e, 0, 1, DC_C_BITS, DC_VALS, 12);
	writeDHT(e, 1, 1, AC_C_BITS, AC_C_VALS, 162);
	put16(e, 0xFFDA);
	put16(e, 12);
	put(e, 3);
	put(e, 1), put(e, 0x00);
	put(e, 2), put(e, 0x11);
	put(e, 3), put(e, 0x11);
	put(e, 0), put(e, 63), put(e, 0);

	int dcY = 0, dcCb = 0, dcCr = 0;
	s16 blk[64];
	int stride = w * 2;
	for (int my = 0; my < h / 8 && !e->fail; my++) {
		for (int mx = 0; mx < w / 16; mx++) {
			const u8* base = yuv + my * 8 * stride + mx * 32;
			for (int b = 0; b < 2; b++) {  // dois blocos de Y
				for (int y = 0; y < 8; y++)
					for (int x = 0; x < 8; x++) blk[y * 8 + x] = (s16)base[y * stride + (b * 8 + x) * 2] - 128;
				dcY = encodeBlock(e, blk, e->rL, &e->dcL, &e->acL, dcY);
			}
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 8; x++) blk[y * 8 + x] = (s16)base[y * stride + x * 4 + 1] - 128;
			dcCb = encodeBlock(e, blk, e->rC, &e->dcC, &e->acC, dcCb);
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 8; x++) blk[y * 8 + x] = (s16)base[y * stride + x * 4 + 3] - 128;
			dcCr = encodeBlock(e, blk, e->rC, &e->dcC, &e->acC, dcCr);
		}
	}
	if (e->nbits > 0) putBits(e, 0x7F, 8 - e->nbits);  // completa com 1s
	put16(e, 0xFFD9);
	int len = e->fail ? -1 : e->len;
	if (e->fail) free(e->out);
	else *outp = e->out;
	free(e);
	return len;
}

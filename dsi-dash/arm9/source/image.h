// Decodificacao de imagens para RGB555 (reduzidas para caber em maxW x maxH)
#pragma once
#include <nds.h>

u16* imgDecodeMem(const u8* data, int len, int maxW, int maxH, int* w, int* h);
u16* imgLoadFile(const char* path, int maxW, int maxH, int* w, int* h);
bool imgIsImageName(const char* name);

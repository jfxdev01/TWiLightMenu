// Codificador JPEG (YUV422 da camera -> arquivo JPEG em memoria)
#pragma once
#include <nds.h>

// retorna o tamanho (e *out alocado com malloc) ou -1
int jpegEncodeYuv422(const u8* yuv, int w, int h, int quality, u8** out);

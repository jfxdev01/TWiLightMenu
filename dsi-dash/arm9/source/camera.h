// Cameras do DSi (so no modo DSi)
#pragma once
#include <nds.h>

#define CAM_PREVIEW_W 256
#define CAM_PREVIEW_H 192
#define CAM_PHOTO_W 640
#define CAM_PHOTO_H 480

enum { CAM_INNER = 0, CAM_OUTER = 1 };

bool camInit(void);                  // liga e configura as duas cameras
void camDeinit(void);
bool camSelect(int dev);
bool camStartPreview(u16* buf);      // 256x192 RGB555 (quadro unico)
bool camStartPhoto(u16* buf);        // 640x480 YUV422 (Y0 Cb Y1 Cr)
bool camBusy(void);                  // transferencia em andamento
void camStop(void);

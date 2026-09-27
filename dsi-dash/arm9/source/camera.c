// DSi Dash — cameras do DSi (lado ARM9): energia, clocks, modo e NDMA.
// Baseado no libnds do BlocksDS (C) 2023 Adrian "asie" Siekierka, licenca zlib.
#include "common.h"
#include <malloc.h>
#include "camera.h"
#include "../../common/ipc.h"

#define REG_CAM_MCNT (*(vu16*)0x4004200)
#define REG_CAM_CNT (*(vu16*)0x4004202)
#define REG_CAM_DATA (*(vu32*)0x4004204)

#define CAM_MCNT_RESET_DISABLE BIT(1)
#define CAM_MCNT_PWR_18V_IO BIT(5)
#define CAM_CNT_SCANLINES(n) ((n) - 1)
#define CAM_CNT_TRANSFER_FLUSH BIT(5)
#define CAM_CNT_IRQ BIT(11)
#define CAM_CNT_FORMAT_YUV (0)
#define CAM_CNT_FORMAT_RGB BIT(13)
#define CAM_CNT_TRANSFER_ENABLE BIT(15)

#define SEQ_PREVIEW 1
#define SEQ_CAPTURE 2
#define CAM_NDMA 1

static int s_mode = -1;
static bool s_on;

static u32 cam7(unsigned sub, unsigned v) { return sysIpc(IPC_CAM, IPC_CAMARG(sub, v)); }

void camDeinit(void) {
	if (REG_CAM_MCNT & CAM_MCNT_PWR_18V_IO) cam7(CAM7_DEINIT, 0);
	camStop();
	REG_CAM_CNT &= ~0x8F00;
	REG_CAM_CNT |= CAM_CNT_TRANSFER_FLUSH;
	REG_SCFG_CLK &= ~SCFG_CLK_CAM_EXT;
	swiDelay(30);
	REG_CAM_MCNT = 0;
	REG_SCFG_CLK &= ~SCFG_CLK_CAM_IFACE;
	swiDelay(30);
	s_mode = -1;
	s_on = false;
}

bool camInit(void) {
	if (!sysIsDSi()) return false;
	if (REG_CAM_MCNT || (REG_SCFG_CLK & (SCFG_CLK_CAM_IFACE | SCFG_CLK_CAM_EXT))) camDeinit();
	REG_SCFG_CLK |= SCFG_CLK_CAM_IFACE;
	REG_CAM_MCNT = 0;
	swiDelay(30);
	REG_SCFG_CLK |= SCFG_CLK_CAM_EXT;
	swiDelay(30);
	REG_CAM_MCNT |= CAM_MCNT_RESET_DISABLE | CAM_MCNT_PWR_18V_IO;
	swiDelay(8200);
	REG_SCFG_CLK &= ~SCFG_CLK_CAM_EXT;
	REG_CAM_CNT &= ~CAM_CNT_TRANSFER_ENABLE;
	REG_CAM_CNT |= CAM_CNT_TRANSFER_FLUSH;
	REG_CAM_CNT = (REG_CAM_CNT & ~0x0300) | 0x0200;
	REG_CAM_CNT |= 0x0400;
	REG_CAM_CNT |= CAM_CNT_IRQ;
	REG_SCFG_CLK |= SCFG_CLK_CAM_EXT;
	swiDelay(20);
	u32 ver = cam7(CAM7_INIT, 0);
	REG_SCFG_CLK &= ~SCFG_CLK_CAM_EXT;
	REG_SCFG_CLK |= SCFG_CLK_CAM_EXT;
	swiDelay(20);
	s_mode = -1;
	s_on = (ver == 0x2280);
	return s_on;
}

bool camSelect(int dev) {
	s_mode = -1;
	return cam7(CAM7_SELECT, dev) != 0;
}

bool camBusy(void) { return ndmaIsBusy(CAM_NDMA) && (REG_CAM_CNT & CAM_CNT_TRANSFER_ENABLE); }

void camStop(void) {
	REG_NDMAxCNT(CAM_NDMA) &= ~NDMA_START;
	REG_CAM_CNT &= ~CAM_CNT_TRANSFER_ENABLE;
}

static bool start(u16* buf, int mode) {
	if (!s_on) return false;
	if (REG_CAM_CNT & CAM_CNT_TRANSFER_ENABLE) camStop();
	if (s_mode != mode) {
		if (!cam7(CAM7_SEQ, mode)) {
			s_mode = -1;
			return false;
		}
		s_mode = mode;
	}
	bool preview = mode == SEQ_PREVIEW;
	u32 words = preview ? (CAM_PREVIEW_W * CAM_PREVIEW_H) / 2 : (CAM_PHOTO_W * CAM_PHOTO_H) / 2;
	DC_FlushRange(buf, words * 4);
	DC_InvalidateRange(buf, words * 4);
	REG_CAM_CNT &= ~0x200F;
	REG_CAM_CNT |= preview ? (CAM_CNT_FORMAT_RGB | CAM_CNT_SCANLINES(4)) : (CAM_CNT_FORMAT_YUV | CAM_CNT_SCANLINES(1));
	REG_CAM_CNT |= CAM_CNT_TRANSFER_FLUSH;
	REG_CAM_CNT |= CAM_CNT_TRANSFER_ENABLE;
	REG_NDMAxSAD(CAM_NDMA) = (u32)&REG_CAM_DATA;
	REG_NDMAxDAD(CAM_NDMA) = (u32)buf;
	REG_NDMAxTCNT(CAM_NDMA) = words;
	REG_NDMAxWCNT(CAM_NDMA) = preview ? 512 : 320;
	REG_NDMAxBCNT(CAM_NDMA) = 2;
	REG_NDMAxCNT(CAM_NDMA) = NDMA_SRC_MODE(NdmaMode_Fixed) | NDMA_DST_MODE(NdmaMode_Increment) | NDMA_BLK_WORDS(16) |
		NDMA_TIMING(NdmaTiming_Camera) | NDMA_TX_MODE(NdmaTxMode_Timing) | NDMA_START;
	return true;
}

bool camStartPreview(u16* buf) { return start(buf, SEQ_PREVIEW); }
bool camStartPhoto(u16* buf) { return start(buf, SEQ_CAPTURE); }

// DSi Dash — cameras do DSi (Aptina MT9V113) pelo I2C do ARM7.
// Portado do libnds do BlocksDS para o calico:
//   Copyright (C) 2023 Adrian "asie" Siekierka; (C) 2011 Dave Murphy; (C) 2023 Epicpkmn11
//   Licenca zlib — ver assets/ref_blocksds_camera. Alterado para o calico (mutex de I2C e PXI).
#include <calico.h>
#include <nds.h>
#include "camera7.h"

#define I2C_CAM0 0x7A  // interna
#define I2C_CAM1 0x78  // externa

#define I2CCNT_STOP BIT(0)
#define I2CCNT_START BIT(1)
#define I2CCNT_ERROR BIT(2)
#define I2CCNT_ACK BIT(4)
#define I2CCNT_READ BIT(5)
#define I2CCNT_ENABLE_IRQ BIT(6)
#define I2CCNT_ENABLE BIT(7)
#define I2CCNT_BUSY BIT(7)

#define I2CREG_APT_APERTURE_GAIN(n) ((n) << 8)
#define I2CREG_APT_APERTURE_GAIN_EXP(n) ((n) << 11)
#define I2CREG_APT_APERTURE_PARAMS 0x326C
#define I2CREG_APT_CHIP_VERSION 0x0000
#define I2CREG_APT_CLKIN_ENABLE (1 << 9)
#define I2CREG_APT_CLOCKS_CNT 0x0016
#define I2CREG_APT_COLOR_PIPELINE_CNT 0x3210
#define I2CREG_APT_I2C_RESET (1 << 0)
#define I2CREG_APT_MCU_ADDRESS 0x098C
#define I2CREG_APT_MCU_DATA0 0x0990
#define I2CREG_APT_MIPI_TX_RESET (1 << 1)
#define I2CREG_APT_PAD_SLEW 0x001E
#define I2CREG_APT_PARALLEL_ENABLE (1 << 9)
#define I2CREG_APT_PARALLEL_OUT_SLEW_RATE(n) (n)
#define I2CREG_APT_PCLK_SLEW_RATE(n) ((n) << 8)
#define I2CREG_APT_PGA_PIXEL_SHADING_CORRECT_ENABLE (1 << 3)
#define I2CREG_APT_PLL_BYPASS (1 << 0)
#define I2CREG_APT_PLL_CNT 0x0014
#define I2CREG_APT_PLL_DIVS 0x0010
#define I2CREG_APT_PLL_ENABLE (1 << 1)
#define I2CREG_APT_PLL_LOCK (1 << 15)
#define I2CREG_APT_PLL_M(n) (n)
#define I2CREG_APT_PLL_N(n) ((n) << 8)
#define I2CREG_APT_PLL_P1(n) (n)
#define I2CREG_APT_PLL_P3(n) ((n) << 8)
#define I2CREG_APT_PLL_P_DIVS 0x0012
#define I2CREG_APT_PLL_RESET_CNTR (1 << 8)
#define I2CREG_APT_RESET_MISC_CNT 0x001A
#define I2CREG_APT_STANDBY_CNT 0x0018
#define I2CREG_APT_STANDBY_ENABLE (1 << 0)
#define I2CREG_APT_STANDBY_IRQ_ENABLE (1 << 3)
#define I2CREG_APT_STANDBY_STATUS (1 << 14)

#define MCUREG_APT_8BIT 0x8000
#define MCUREG_APT_16BIT 0x0000
#define MCUREG_APT_AE_MAX_INDEX (MCUREG_APT_8BIT | 0x220C)
#define MCUREG_APT_AE_MIN_INDEX (MCUREG_APT_8BIT | 0x220B)
#define MCUREG_APT_AE_TARGET_BASE (MCUREG_APT_8BIT | 0x224F)
#define MCUREG_APT_AE_TARGET_BUFFER_SPEED (MCUREG_APT_8BIT | 0x224C)
#define MCUREG_APT_AE_WINDOW_HEIGHT(n) ((n) << 4)
#define MCUREG_APT_AE_WINDOW_POS (MCUREG_APT_8BIT | 0x2202)
#define MCUREG_APT_AE_WINDOW_SIZE (MCUREG_APT_8BIT | 0x2203)
#define MCUREG_APT_AE_WINDOW_WIDTH(n) (n)
#define MCUREG_APT_AE_WINDOW_X0(n) (n)
#define MCUREG_APT_AE_WINDOW_Y0(n) ((n) << 4)
#define MCUREG_APT_HG_LL_AP_CORR1 (MCUREG_APT_8BIT | 0x2B22)
#define MCUREG_APT_MODE_A_OUTPUT_FORMAT (MCUREG_APT_16BIT | 0x2755)
#define MCUREG_APT_MODE_A_OUTPUT_HEIGHT (MCUREG_APT_16BIT | 0x2705)
#define MCUREG_APT_MODE_A_OUTPUT_WIDTH (MCUREG_APT_16BIT | 0x2703)
#define MCUREG_APT_MODE_A_SENSOR_FINE_CORRECTION (MCUREG_APT_16BIT | 0x2719)
#define MCUREG_APT_MODE_A_SENSOR_FINE_IT_MAX_MARGIN (MCUREG_APT_16BIT | 0x271D)
#define MCUREG_APT_MODE_A_SENSOR_FINE_IT_MIN (MCUREG_APT_16BIT | 0x271B)
#define MCUREG_APT_MODE_A_SENSOR_FRAME_LENGTH (MCUREG_APT_16BIT | 0x271F)
#define MCUREG_APT_MODE_A_SENSOR_LINE_LENGTH_PCK (MCUREG_APT_16BIT | 0x2721)
#define MCUREG_APT_MODE_A_SENSOR_READ_MODE (MCUREG_APT_16BIT | 0x2717)
#define MCUREG_APT_MODE_A_SENSOR_ROW_SPEED (MCUREG_APT_16BIT | 0x2715)
#define MCUREG_APT_MODE_B_OUTPUT_FORMAT (MCUREG_APT_16BIT | 0x2757)
#define MCUREG_APT_MODE_B_OUTPUT_HEIGHT (MCUREG_APT_16BIT | 0x2709)
#define MCUREG_APT_MODE_B_OUTPUT_WIDTH (MCUREG_APT_16BIT | 0x2707)
#define MCUREG_APT_MODE_B_SENSOR_FINE_CORRECTION (MCUREG_APT_16BIT | 0x272F)
#define MCUREG_APT_MODE_B_SENSOR_FINE_IT_MAX_MARGIN (MCUREG_APT_16BIT | 0x2733)
#define MCUREG_APT_MODE_B_SENSOR_FINE_IT_MIN (MCUREG_APT_16BIT | 0x2731)
#define MCUREG_APT_MODE_B_SENSOR_FRAME_LENGTH (MCUREG_APT_16BIT | 0x2735)
#define MCUREG_APT_MODE_B_SENSOR_LINE_LENGTH_PCK (MCUREG_APT_16BIT | 0x2737)
#define MCUREG_APT_MODE_B_SENSOR_READ_MODE (MCUREG_APT_16BIT | 0x272D)
#define MCUREG_APT_MODE_B_SENSOR_ROW_SPEED (MCUREG_APT_16BIT | 0x272B)
#define MCUREG_APT_MODE_OUTPUT_FORMAT_SWAP_LUMA_CHROMA (1 << 1)
#define MCUREG_APT_MODE_OUTPUT_FORMAT_YUV (0)
#define MCUREG_APT_READ_X_MIRROR (1 << 0)
#define MCUREG_APT_READ_X_ODD_INC(n) ((n) << 5)
#define MCUREG_APT_READ_Y_ODD_INC(n) ((n) << 2)
#define MCUREG_APT_SEQ_CAP_MODE (MCUREG_APT_8BIT | 0x2115)
#define MCUREG_APT_SEQ_CAP_MODE_VIDEO_AWB_ENABLE (1 << 4)
#define MCUREG_APT_SEQ_CAP_MODE_VIDEO_ENABLE (1 << 1)
#define MCUREG_APT_SEQ_CAP_MODE_VIDEO_HG_ENABLE (1 << 5)
#define MCUREG_APT_SEQ_CMD (MCUREG_APT_8BIT | 0x2103)
#define MCUREG_APT_SEQ_CMD_MASK (0xFF)
#define MCUREG_APT_SEQ_CMD_REFRESH (5)
#define MCUREG_APT_SEQ_CMD_REFRESH_MODE (6)
#define MCUREG_APT_SEQ_PREVIEW1_AWB (MCUREG_APT_8BIT | 0x211F)

// ---------------------------------------------------------------------------
// I2C de baixo nivel (enderecos e dados de 16 bits das cameras Aptina)
// ---------------------------------------------------------------------------
static inline void camI2cWaitBusy(void) {
	while (REG_I2C_CNT & I2CCNT_BUSY);
}

static u8 i2cResult(void) {
	camI2cWaitBusy();
	return (REG_I2C_CNT >> 4) & 1;
}

static u8 aptGetData(u8 flags) {
	REG_I2C_CNT = I2CCNT_ENABLE | I2CCNT_ENABLE_IRQ | flags;
	camI2cWaitBusy();
	return REG_I2C_DATA;
}

static u8 aptSetData(u8 data, u8 flags) {
	REG_I2C_DATA = data;
	REG_I2C_CNT = I2CCNT_ENABLE | I2CCNT_ENABLE_IRQ | flags;
	return i2cResult();
}

static u8 aptSelectDevice(u8 dev, u8 flags) {
	camI2cWaitBusy();
	REG_I2C_DATA = dev;
	REG_I2C_CNT = I2CCNT_ENABLE | I2CCNT_ENABLE_IRQ | flags;
	return i2cResult();
}

static u8 aptSelectRegister(u8 reg, u8 flags) {
	camI2cWaitBusy();
	REG_I2C_DATA = reg;
	REG_I2C_CNT = I2CCNT_ENABLE | I2CCNT_ENABLE_IRQ | flags;
	return i2cResult();
}

static u8 aptI2cWrite(u8 dev, u16 reg, u16 data) {
	u8 ok = 0;
	i2cLock();
	for (int i = 0; i < 8 && !ok; i++) {
		if (aptSelectDevice(dev, I2CCNT_START) && aptSelectRegister(reg >> 8, 0) && aptSelectRegister(reg & 0xFF, 0)) {
			camI2cWaitBusy();
			if (aptSetData(data >> 8, 0) && aptSetData(data & 0xFF, I2CCNT_STOP)) ok = 1;
		}
		if (!ok) REG_I2C_CNT = I2CCNT_ENABLE | I2CCNT_ENABLE_IRQ | I2CCNT_ERROR | I2CCNT_STOP;
	}
	i2cUnlock();
	return ok;
}

static u16 aptI2cRead(u8 dev, u16 reg) {
	u16 v = 0xFFFF;
	i2cLock();
	for (int i = 0; i < 8; i++) {
		if (aptSelectDevice(dev, I2CCNT_START) && aptSelectRegister(reg >> 8, 0) && aptSelectRegister(reg & 0xFF, I2CCNT_STOP)) {
			camI2cWaitBusy();
			if (aptSelectDevice(dev | 1, I2CCNT_START)) {
				v = aptGetData(I2CCNT_READ | I2CCNT_ACK) << 8;
				v |= aptGetData(I2CCNT_STOP | I2CCNT_READ);
				break;
			}
		}
		REG_I2C_CNT = I2CCNT_ENABLE | I2CCNT_ENABLE_IRQ | I2CCNT_ERROR | I2CCNT_STOP;
	}
	i2cUnlock();
	return v;
}

// esperas com limite (nao trava o ARM7 se a camera nao responder)
static bool aptI2cWaitClear(u8 dev, u16 reg, u16 mask) {
	for (int i = 0; i < 2000; i++) {
		if (!(aptI2cRead(dev, reg) & mask)) return true;
		threadSleep(500);
	}
	return false;
}

static bool aptI2cWaitSet(u8 dev, u16 reg, u16 mask) {
	for (int i = 0; i < 2000; i++) {
		if ((aptI2cRead(dev, reg) & mask) == mask) return true;
		threadSleep(500);
	}
	return false;
}

static void aptI2cClearBits(u8 dev, u16 reg, u16 mask) { aptI2cWrite(dev, reg, aptI2cRead(dev, reg) & ~mask); }
static void aptI2cSetBits(u8 dev, u16 reg, u16 mask) { aptI2cWrite(dev, reg, aptI2cRead(dev, reg) | mask); }

static u16 aptMcuRead(u8 dev, u16 reg) {
	aptI2cWrite(dev, I2CREG_APT_MCU_ADDRESS, reg);
	return aptI2cRead(dev, I2CREG_APT_MCU_DATA0);
}

static void aptMcuWrite(u8 dev, u16 reg, u16 data) {
	aptI2cWrite(dev, I2CREG_APT_MCU_ADDRESS, reg);
	aptI2cWrite(dev, I2CREG_APT_MCU_DATA0, data);
}

static void aptMcuSetBits(u8 dev, u16 reg, u16 mask) { aptMcuWrite(dev, reg, aptMcuRead(dev, reg) | mask); }

static void aptSeqCmd(u8 dev, u8 cmd) {
	aptMcuWrite(dev, MCUREG_APT_SEQ_CMD, cmd);
	for (int i = 0; i < 2000; i++) {
		if (!(aptMcuRead(dev, MCUREG_APT_SEQ_CMD) & MCUREG_APT_SEQ_CMD_MASK)) break;
		threadSleep(500);
	}
}

static void camLed(u8 v) {
	i2cLock();
	i2cWriteRegister8(I2cDev_MCU, McuReg_CamLed, v);
	i2cUnlock();
}

static void aptWakeup(u8 dev) {
	aptI2cClearBits(dev, I2CREG_APT_STANDBY_CNT, I2CREG_APT_STANDBY_ENABLE);
	aptI2cWaitClear(dev, I2CREG_APT_STANDBY_CNT, I2CREG_APT_STANDBY_STATUS);
	aptI2cWaitSet(dev, 0x301A, 0x0004);
}

static void aptStandby(u8 dev) {
	aptI2cSetBits(dev, I2CREG_APT_STANDBY_CNT, I2CREG_APT_STANDBY_ENABLE);
	aptI2cWaitSet(dev, I2CREG_APT_STANDBY_CNT, I2CREG_APT_STANDBY_STATUS);
	aptI2cWaitClear(dev, 0x301A, 0x0004);
}

static void aptInit(u8 dev) {
	aptI2cWrite(dev, I2CREG_APT_RESET_MISC_CNT, I2CREG_APT_MIPI_TX_RESET | I2CREG_APT_I2C_RESET);
	aptI2cWrite(dev, I2CREG_APT_RESET_MISC_CNT, 0);
	aptI2cWrite(dev, I2CREG_APT_STANDBY_CNT, I2CREG_APT_STANDBY_STATUS | I2CREG_APT_STANDBY_IRQ_ENABLE | (1 << 5));
	aptI2cWrite(dev, I2CREG_APT_PAD_SLEW, I2CREG_APT_PARALLEL_OUT_SLEW_RATE(1) | I2CREG_APT_PCLK_SLEW_RATE(2));
	aptI2cWrite(dev, I2CREG_APT_CLOCKS_CNT, I2CREG_APT_CLKIN_ENABLE | 0x40DF);
	aptI2cWaitClear(dev, I2CREG_APT_STANDBY_CNT, I2CREG_APT_STANDBY_STATUS);
	aptI2cWaitSet(dev, 0x301A, 0x0004);

	aptMcuWrite(dev, 0x02F0, 0x0000);
	aptMcuWrite(dev, 0x02F2, 0x0210);
	aptMcuWrite(dev, 0x02F4, 0x001A);
	aptMcuWrite(dev, 0x2145, 0x02F4);
	aptMcuWrite(dev, MCUREG_APT_8BIT | 0x2134, 0x01);

	aptMcuSetBits(dev, MCUREG_APT_SEQ_CAP_MODE, MCUREG_APT_SEQ_CAP_MODE_VIDEO_ENABLE);
	aptMcuWrite(dev, MCUREG_APT_MODE_A_OUTPUT_FORMAT, MCUREG_APT_MODE_OUTPUT_FORMAT_YUV | MCUREG_APT_MODE_OUTPUT_FORMAT_SWAP_LUMA_CHROMA);
	aptMcuWrite(dev, MCUREG_APT_MODE_B_OUTPUT_FORMAT, MCUREG_APT_MODE_OUTPUT_FORMAT_YUV | MCUREG_APT_MODE_OUTPUT_FORMAT_SWAP_LUMA_CHROMA);

	// PLL casado com o clock do console
	aptI2cWrite(dev, I2CREG_APT_PLL_CNT, 0x2044 | I2CREG_APT_PLL_RESET_CNTR | I2CREG_APT_PLL_BYPASS);
	aptI2cWrite(dev, I2CREG_APT_PLL_DIVS, I2CREG_APT_PLL_M(17) | I2CREG_APT_PLL_N(1));
	aptI2cWrite(dev, I2CREG_APT_PLL_P_DIVS, I2CREG_APT_PLL_P1(0) | I2CREG_APT_PLL_P3(0));
	aptI2cWrite(dev, I2CREG_APT_PLL_CNT, 0x2448 | I2CREG_APT_PLL_ENABLE | I2CREG_APT_PLL_BYPASS);
	aptI2cWrite(dev, I2CREG_APT_PLL_CNT, 0x3048 | I2CREG_APT_PLL_ENABLE | I2CREG_APT_PLL_BYPASS);
	aptI2cWaitSet(dev, I2CREG_APT_PLL_CNT, I2CREG_APT_PLL_LOCK);
	aptI2cClearBits(dev, I2CREG_APT_PLL_CNT, I2CREG_APT_PLL_BYPASS);

	// modo A = previa 256x192, modo B = foto 640x480
	aptMcuWrite(dev, MCUREG_APT_MODE_A_OUTPUT_WIDTH, 256);
	aptMcuWrite(dev, MCUREG_APT_MODE_A_OUTPUT_HEIGHT, 192);
	aptMcuWrite(dev, MCUREG_APT_MODE_B_OUTPUT_WIDTH, 640);
	aptMcuWrite(dev, MCUREG_APT_MODE_B_OUTPUT_HEIGHT, 480);

	u16 readMode = MCUREG_APT_READ_X_ODD_INC(1) | MCUREG_APT_READ_Y_ODD_INC(1);
	if (dev == I2C_CAM1) readMode |= MCUREG_APT_READ_X_MIRROR;

	aptMcuWrite(dev, MCUREG_APT_MODE_A_SENSOR_ROW_SPEED, 1);
	aptMcuWrite(dev, MCUREG_APT_MODE_A_SENSOR_FINE_CORRECTION, 26);
	aptMcuWrite(dev, MCUREG_APT_MODE_A_SENSOR_FINE_IT_MIN, 107);
	aptMcuWrite(dev, MCUREG_APT_MODE_A_SENSOR_FINE_IT_MAX_MARGIN, 107);
	aptMcuWrite(dev, MCUREG_APT_MODE_A_SENSOR_FRAME_LENGTH, 704);
	aptMcuWrite(dev, MCUREG_APT_MODE_A_SENSOR_LINE_LENGTH_PCK, 843);
	aptMcuWrite(dev, MCUREG_APT_AE_MIN_INDEX, 0);
	aptMcuWrite(dev, MCUREG_APT_AE_MAX_INDEX, 6);
	aptMcuWrite(dev, MCUREG_APT_MODE_B_SENSOR_ROW_SPEED, 1);
	aptMcuWrite(dev, MCUREG_APT_MODE_B_SENSOR_FINE_CORRECTION, 26);
	aptMcuWrite(dev, MCUREG_APT_MODE_B_SENSOR_FINE_IT_MIN, 107);
	aptMcuWrite(dev, MCUREG_APT_MODE_B_SENSOR_FINE_IT_MAX_MARGIN, 107);
	aptMcuWrite(dev, MCUREG_APT_MODE_B_SENSOR_FRAME_LENGTH, 704);
	aptMcuWrite(dev, MCUREG_APT_MODE_B_SENSOR_LINE_LENGTH_PCK, 843);
	aptI2cSetBits(dev, I2CREG_APT_COLOR_PIPELINE_CNT, I2CREG_APT_PGA_PIXEL_SHADING_CORRECT_ENABLE);
	aptMcuWrite(dev, MCUREG_APT_8BIT | 0x2208, 0x00);
	aptMcuWrite(dev, MCUREG_APT_AE_TARGET_BUFFER_SPEED, 32);
	aptMcuWrite(dev, MCUREG_APT_AE_TARGET_BASE, 112);
	aptMcuWrite(dev, MCUREG_APT_MODE_A_SENSOR_READ_MODE, readMode);
	aptMcuWrite(dev, MCUREG_APT_MODE_B_SENSOR_READ_MODE, readMode);
	if (dev == I2C_CAM0) {
		aptMcuWrite(dev, MCUREG_APT_AE_WINDOW_POS, MCUREG_APT_AE_WINDOW_X0(2) | MCUREG_APT_AE_WINDOW_Y0(2));
		aptMcuWrite(dev, MCUREG_APT_AE_WINDOW_SIZE, MCUREG_APT_AE_WINDOW_WIDTH(11) | MCUREG_APT_AE_WINDOW_HEIGHT(11));
	} else {
		aptMcuWrite(dev, MCUREG_APT_AE_WINDOW_POS, MCUREG_APT_AE_WINDOW_X0(0) | MCUREG_APT_AE_WINDOW_Y0(0));
		aptMcuWrite(dev, MCUREG_APT_AE_WINDOW_SIZE, MCUREG_APT_AE_WINDOW_WIDTH(15) | MCUREG_APT_AE_WINDOW_HEIGHT(15));
	}
	aptI2cSetBits(dev, I2CREG_APT_CLOCKS_CNT, 1 << 5);
	aptMcuWrite(dev, MCUREG_APT_SEQ_CAP_MODE, 0x40 | MCUREG_APT_SEQ_CAP_MODE_VIDEO_HG_ENABLE | MCUREG_APT_SEQ_CAP_MODE_VIDEO_AWB_ENABLE | MCUREG_APT_SEQ_CAP_MODE_VIDEO_ENABLE);
	aptMcuWrite(dev, MCUREG_APT_SEQ_PREVIEW1_AWB, 0x01);
	if (dev == I2C_CAM0) {
		aptI2cWrite(dev, I2CREG_APT_APERTURE_PARAMS, I2CREG_APT_APERTURE_GAIN(1) | I2CREG_APT_APERTURE_GAIN_EXP(1));
		aptMcuWrite(dev, MCUREG_APT_HG_LL_AP_CORR1, 1);
	} else {
		aptI2cWrite(dev, I2CREG_APT_APERTURE_PARAMS, I2CREG_APT_APERTURE_GAIN(0) | I2CREG_APT_APERTURE_GAIN_EXP(2));
		aptMcuWrite(dev, MCUREG_APT_HG_LL_AP_CORR1, 2);
	}
	aptSeqCmd(dev, MCUREG_APT_SEQ_CMD_REFRESH_MODE);
	aptSeqCmd(dev, MCUREG_APT_SEQ_CMD_REFRESH);
}

static void aptActivate(u8 dev) {
	if (dev == 0xFF) return;
	aptWakeup(dev);
	aptI2cSetBits(dev, I2CREG_APT_RESET_MISC_CNT, I2CREG_APT_PARALLEL_ENABLE);
	if (dev == I2C_CAM1) camLed(1);
}

static void aptDeactivate(u8 dev) {
	if (dev == 0xFF) return;
	aptI2cClearBits(dev, I2CREG_APT_RESET_MISC_CNT, I2CREG_APT_PARALLEL_ENABLE);
	aptStandby(dev);
	if (dev == I2C_CAM1) camLed(0);
}

static u8 s_active = 0xFF;

u32 camera7Command(unsigned sub, unsigned val) {
	switch (sub) {
		case CAM7_INIT:
			aptInit(I2C_CAM0);
			aptInit(I2C_CAM1);
			// sem isso, no DSi a imagem sai invertida e com cores trocadas (nota do BlocksDS)
			aptDeactivate(I2C_CAM0);
			aptDeactivate(I2C_CAM1);
			s_active = 0xFF;
			return aptI2cRead(I2C_CAM0, I2CREG_APT_CHIP_VERSION);
		case CAM7_DEINIT:
			aptDeactivate(s_active);
			s_active = 0xFF;
			return 1;
		case CAM7_SELECT:
			aptDeactivate(s_active);
			s_active = val > 1 ? 0xFF : (val == 1 ? I2C_CAM1 : I2C_CAM0);
			aptActivate(s_active);
			return 1;
		case CAM7_SEQ:
			if (s_active == 0xFF) return 0;
			aptSeqCmd(s_active, val & MCUREG_APT_SEQ_CMD_MASK);
			return 1;
	}
	return 0;
}

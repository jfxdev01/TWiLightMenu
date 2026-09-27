// DSi Dash — ARM7
// Default calico ARM7 services + a small PXI server for things only the ARM7
// can reach (DSi MCU over I2C: backlight, warm reboot into Unlaunch, power off).
#include <calico.h>
#include <nds.h>
#include <maxmod7.h>
#include <string.h>

#include "../../common/ipc.h"
#include "camera7.h"

static Thread s_srvThread;
alignas(8) static u8 s_srvStack[4096];

static void rebootToUnlaunch(const void* block) {
	// ARM9 is parked in ITCM at this point, so main RAM around 0x02000800 is free
	memcpy((void*)0x02000800, block, 0x400);
	i2cLock();
	i2cWriteRegister8(I2cDev_MCU, McuReg_WarmbootFlag, 1);
	i2cUnlock();
	mcuIssueReset();
}

static int srvMain(void* arg) {
	Mailbox mb;
	u32 slots[4];
	mailboxPrepare(&mb, slots, sizeof(slots) / 4);
	pxiSetMailbox(PxiChannel_User0, &mb);

	for (;;) {
		u32 msg = mailboxRecv(&mb);
		unsigned cmd = IPC_CMD(msg);
		u32 arg = IPC_ARG(msg);
		u32 ret = 0;
		bool twl = systemIsTwlMode();
		switch (cmd) {
			case IPC_PING:
				ret = twl ? 2 : 1;
				break;
			case IPC_BACKLIGHT_SET:
				if (twl) {
					i2cLock();
					i2cWriteRegister8(I2cDev_MCU, McuReg_BacklightLevel, arg > 4 ? 4 : arg);
					i2cUnlock();
					ret = 1;
				}
				break;
			case IPC_BACKLIGHT_GET:
				if (twl) {
					i2cLock();
					ret = i2cReadRegister8(I2cDev_MCU, McuReg_BacklightLevel) + 1;
					i2cUnlock();
				}
				break;
			case IPC_REBOOT_UNLAUNCH:
				if (twl) {
					pxiReply(PxiChannel_User0, 1);
					threadSleep(20000);
					rebootToUnlaunch((const void*)IPC_PTR(arg));
				}
				break;
			case IPC_CAM:
				ret = twl ? camera7Command((arg >> 16) & 0x3F, arg & 0xFFFF) : 0;
				break;
			case IPC_SHUTDOWN:
				if (twl) {
					pxiReply(PxiChannel_User0, 1);
					threadSleep(20000);
					mcuIssueShutdown();
				}
				break;
			default:
				break;
		}
		pxiReply(PxiChannel_User0, ret);
	}
	return 0;
}

int main(void) {
	envReadNvramSettings();
	keypadStartExtServer();

	lcdSetIrqMask(DISPSTAT_IE_ALL, DISPSTAT_IE_VBLANK);
	irqEnable(IRQ_VBLANK);

	rtcInit();
	rtcSyncTime();

	pmInit();
	blkInit();

	touchInit();
	touchStartServer(80, MAIN_THREAD_PRIO);

	soundStartServer(MAIN_THREAD_PRIO - 0x10);
	micStartServer(MAIN_THREAD_PRIO - 0x18);

	wlmgrStartServer(MAIN_THREAD_PRIO - 8);

	mmInstall(MAIN_THREAD_PRIO + 1);

	threadPrepare(&s_srvThread, srvMain, NULL, &s_srvStack[sizeof(s_srvStack)], MAIN_THREAD_PRIO - 4);
	threadStart(&s_srvThread);

	while (pmMainLoop()) {
		threadWaitForVBlank();
	}
	return 0;
}

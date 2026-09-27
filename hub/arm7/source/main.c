// SPDX-License-Identifier: Zlib
//
// ARM7 core of TWiLight Hub. Based on the default BlocksDS ARM7 core, plus:
// - DSWiFi (DS and DSi mode)
// - DSi camera driver
// - A FIFO command to write the real time clock (used by the clock sync app)

#include <nds.h>
#include <dswifi7.h>

#include "../../arm9/source/ipc_commands.h"

volatile bool exit_loop = false;

static void power_button_callback(void)
{
	exit_loop = true;
}

static void vblank_handler(void)
{
	inputGetAndSend();
	Wifi_Update();
}

static void rtc_set_handler(int num_bytes, void *userdata)
{
	(void)userdata;

	rtcTimeAndDate rtc;
	if (num_bytes != sizeof(rtc)) {
		u8 discard[32];
		fifoGetDatamsg(FIFO_HUB_RTC, sizeof(discard), discard);
		fifoSendValue32(FIFO_HUB_RTC, 0);
		return;
	}

	fifoGetDatamsg(FIFO_HUB_RTC, sizeof(rtc), (u8 *)&rtc);

	u32 ok = (rtcTimeAndDateSet(&rtc) == 0) ? 1 : 0;

	// Read the RTC back so that time() on the ARM9 reflects the new value
	resyncClock();

	fifoSendValue32(FIFO_HUB_RTC, ok);
}

int main(void)
{
	enableSound();
	readUserSettings();
	ledBlink(LED_ALWAYS_ON);
	touchInit();

	irqInit();
	fifoInit();

	installWifiFIFO();
	installSoundFIFO();
	installSystemFIFO();
	if (isDSiMode())
		installCameraFIFO();

	setPowerButtonCB(power_button_callback);

	initClockIRQTimer(LIBNDS_DEFAULT_TIMER_RTC);

	fifoSetDatamsgHandler(FIFO_HUB_RTC, rtc_set_handler, NULL);

	irqSet(IRQ_VBLANK, vblank_handler);
	irqEnable(IRQ_VBLANK);

	while (!exit_loop)
		swiWaitForVBlank();

	return 0;
}

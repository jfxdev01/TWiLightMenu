// Shared between the ARM9 and the ARM7 of TWiLight Hub
#pragma once

// Datamsg with a rtcTimeAndDate struct. The ARM7 answers with a value32:
// 1 = the RTC was written, 0 = invalid date.
#define FIFO_HUB_RTC FIFO_USER_01

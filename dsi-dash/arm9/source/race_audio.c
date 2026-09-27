// DSi Dash Racing — som por PSG (canais 11..13 quadrados, 14..15 ruido)
#include "race.h"

#define CH_ENG 11
#define CH_ENG2 12
#define CH_BEEP 13
#define CH_TIRE 14
#define CH_BUMP 15

static bool s_on;
static int s_beepT, s_bumpT;
static float s_rpmS;

void raInit(void) {
	if (s_on) return;
	s_on = true;
	soundPreparePsg(CH_ENG | SOUND_START, 0, 64, soundTimerFromHz(8 * 60), SoundDuty_25);
	soundPreparePsg(CH_ENG2 | SOUND_START, 0, 64, soundTimerFromHz(8 * 90), SoundDuty_12_5);
	soundPreparePsg(CH_TIRE | SOUND_START, 0, 64, soundTimerFromHz(8 * 2000), SoundDuty_50);
	s_rpmS = 0.25f;
}

void raStop(void) {
	if (!s_on) return;
	soundStop((1u << CH_ENG) | (1u << CH_ENG2) | (1u << CH_BEEP) | (1u << CH_TIRE) | (1u << CH_BUMP));
	s_on = false;
}

void raBeep(int hz, int frames, int vol) {
	if (!g_set.sounds) return;
	soundPreparePsg(CH_BEEP | SOUND_START, vol * 3, 64, soundTimerFromHz(8 * hz), SoundDuty_50);
	s_beepT = frames;
}

void raBump(void) {
	if (!g_set.sounds) return;
	soundPreparePsg(CH_BUMP | SOUND_START, 700, 64, soundTimerFromHz(8 * 900), SoundDuty_50);
	s_bumpT = 6;
}

void raUpdate(void) {
	if (!s_on) return;
	if (s_beepT > 0 && --s_beepT == 0) soundStop(1u << CH_BEEP);
	if (s_bumpT > 0 && --s_bumpT == 0) soundStop(1u << CH_BUMP);
	if (!g_set.sounds || R.state == RS_PAUSE || R.state == RS_TITLE) {
		soundChSetVolume(CH_ENG, 0);
		soundChSetVolume(CH_ENG2, 0);
		soundChSetVolume(CH_TIRE, 0);
		return;
	}
	Car* c = rgPlayer();
	s_rpmS += (c->rpm - s_rpmS) * 0.35f;
	int hz = 48 + (int)(s_rpmS * 150) + c->gear * 6;
	soundChSetTimer(CH_ENG, soundTimerFromHz(8 * hz));
	soundChSetTimer(CH_ENG2, soundTimerFromHz(8 * (hz * 3 / 2)));
	int vol = 240 + (int)(s_rpmS * 260) + (c->boost > 0 ? 120 : 0);
	if (R.state == RS_FINISH) vol /= 2;
	soundChSetVolume(CH_ENG, vol);
	soundChSetVolume(CH_ENG2, vol / 3);
	int tire = 0;
	if (c->drifting) tire = 420 + (int)(c->charge * 60);
	else if (c->offroad && c->speed > 8) tire = 200;
	soundChSetVolume(CH_TIRE, tire);
	soundChSetTimer(CH_TIRE, soundTimerFromHz(8 * (c->drifting ? 2600 : 900)));
}

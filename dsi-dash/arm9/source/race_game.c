// DSi Dash Racing — fisica arcade, IA, voltas, particulas e camera
#include "race.h"
#include <math.h>

Race R;

#define VMAX 47.0f          // ~170 km/h
#define VBOOST 13.0f
#define V_OFF 21.0f         // maximo fora do asfalto
#define WALL (RT_HALF_W + RT_CURB_W + RT_RUNOFF - 1.1f)
#define DRIFT_MIN 1.1f      // carga para o turbo azul
#define DRIFT_MAX 2.3f      // carga para o turbo laranja

static const u16 CAR_COLORS[8] = {
	RGB15(29, 3, 4), RGB15(2, 12, 29), RGB15(30, 24, 2), RGB15(4, 22, 8),
	RGB15(28, 28, 28), RGB15(18, 6, 26), RGB15(31, 14, 2), RGB15(4, 24, 27),
};

Car* rgPlayer(void) { return &R.car[0]; }

static void places(void);

static inline float frand(void) { return (rand() & 0xFFFF) / 65535.0f; }

void rgSecPos(int i, float* x, float* z) {
	i = ((i % RT_N) + RT_N) % RT_N;
	*x = g_rt[i].x * (1.0f / 4096);
	*z = g_rt[i].z * (1.0f / 4096);
}

void rgSecRight(int i, float* rx, float* rz) {
	i = ((i % RT_N) + RT_N) % RT_N;
	*rx = g_rt[i].rx * (1.0f / 4096);
	*rz = g_rt[i].rz * (1.0f / 4096);
}

static float secHeading(int i) {
	i = ((i % RT_N) + RT_N) % RT_N;
	return atan2f(g_rt[i].tx * (1.0f / 4096), -g_rt[i].tz * (1.0f / 4096));
}

// secao mais proxima (busca local a partir da anterior), deslocamento lateral e distancia
void rgProject(Car* c) {
	int best = c->seg;
	float bd = 1e9f;
	for (int k = -8; k <= 8; k++) {
		int i = ((c->seg + k) % RT_N + RT_N) % RT_N;
		float sx, sz;
		rgSecPos(i, &sx, &sz);
		float d = (c->x - sx) * (c->x - sx) + (c->z - sz) * (c->z - sz);
		if (d < bd) {
			bd = d;
			best = i;
		}
	}
	c->seg = best;
	float sx, sz, rx, rz;
	rgSecPos(best, &sx, &sz);
	rgSecRight(best, &rx, &rz);
	float tx = g_rt[best].tx * (1.0f / 4096), tz = g_rt[best].tz * (1.0f / 4096);
	c->lat = (c->x - sx) * rx + (c->z - sz) * rz;
	float along = (c->x - sx) * tx + (c->z - sz) * tz;
	float s = best * RT_SEG_LEN + along - RT_START_SEG * RT_SEG_LEN;
	while (s < 0) s += RT_LEN;
	while (s >= RT_LEN) s -= RT_LEN;
	c->s = s;
}

void rgEmit(float x, float y, float z, float vx, float vy, float vz, float life, float size, u16 color, u8 alpha) {
	static int next;
	for (int k = 0; k < MAX_PART; k++) {
		Part* p = &R.part[(next + k) % MAX_PART];
		if (p->life > 0) continue;
		*p = (Part){x, y, z, vx, vy, vz, life, life, size, color, alpha};
		next = (next + k + 1) % MAX_PART;
		return;
	}
}

// ---------------------------------------------------------------------------
void rgReset(void) {
	memset(R.part, 0, sizeof(R.part));
	R.nCars = NCARS;
	R.t = 0;
	R.finishedCount = 0;
	R.rocketStart = false;
	R.autopilot = false;
	R.autoUsed = false;
	R.msgT = 0;
	static const float SKILL[3] = {0.84f, 0.93f, 1.0f};
	// grid: 3 fileiras de 2, jogador na 4a posicao
	static const int ORDER[NCARS] = {3, 0, 1, 2, 4, 5};  // posicao no grid de cada carro
	int ai = 0;
	for (int k = 0; k < NCARS; k++) {
		Car* c = &R.car[k];
		memset(c, 0, sizeof(*c));
		int slot = ORDER[k];
		int row = slot / 2, col = slot % 2;
		int seg = RT_START_SEG - 2 - row * 2;
		float sx, sz, rx, rz;
		rgSecPos(seg, &sx, &sz);
		rgSecRight(seg, &rx, &rz);
		float lat = col ? 3.6f : -3.6f;
		float back = col ? 4.0f : 0;  // fileira escalonada
		float h = secHeading(seg);
		c->x = sx + rx * lat - sinf(h) * back;
		c->z = sz + rz * lat + cosf(h) * back;
		c->h = h;
		c->seg = seg;
		c->ai = k != 0;
		c->color = k == 0 ? CAR_COLORS[R.playerColor & 7] : CAR_COLORS[(R.playerColor + k) & 7];
		c->skill = c->ai ? SKILL[R.difficulty] * (0.955f + 0.015f * ai++) : 1;
		c->laneBias = c->ai ? (frand() - 0.5f) * 3.0f : 0;
		c->best = 0;
		c->gear = 1;
		rgProject(c);
		c->half = false;
		c->lap = 0;
		c->raceDist = -(float)slot;  // ordem do grid ate a largada
	}
	R.camH = R.car[0].h;
	places();
}

// ---------------------------------------------------------------------------
static void emitDrift(Car* c) {
	float fx = sinf(c->h), fz = -cosf(c->h), rx = cosf(c->h), rz = sinf(c->h);
	u16 col = c->charge >= DRIFT_MAX ? RGB15(31, 18, 4) : (c->charge >= DRIFT_MIN ? RGB15(8, 20, 31) : RGB15(26, 26, 26));
	for (int side = -1; side <= 1; side += 2) {
		float x = c->x - fx * 1.5f + rx * 0.9f * side, z = c->z - fz * 1.5f + rz * 0.9f * side;
		rgEmit(x, 0.3f, z, (frand() - 0.5f) * 3 - fx * 3, 1.5f + frand() * 2, (frand() - 0.5f) * 3 - fz * 3, 0.35f, 0.45f, col, 30);
	}
}

static void emitBoost(Car* c) {
	float fx = sinf(c->h), fz = -cosf(c->h);
	u16 col = frand() < 0.5f ? RGB15(31, 22, 6) : RGB15(31, 12, 2);
	rgEmit(c->x - fx * 2.3f, 0.55f, c->z - fz * 2.3f, -fx * 6 + (frand() - 0.5f) * 2, frand(), -fz * 6 + (frand() - 0.5f) * 2, 0.22f, 0.7f, col, 31);
}

static void emitDust(Car* c, bool sand) {
	float fx = sinf(c->h), fz = -cosf(c->h);
	u16 col = sand ? RGB15(27, 24, 17) : RGB15(17, 20, 10);
	rgEmit(c->x - fx * 2.0f + (frand() - 0.5f) * 2, 0.4f, c->z - fz * 2.0f + (frand() - 0.5f) * 2, (frand() - 0.5f) * 2, 1 + frand(), (frand() - 0.5f) * 2, 0.7f, 1.6f, col, 16);
}

static void updateParticles(float dt) {
	for (int k = 0; k < MAX_PART; k++) {
		Part* p = &R.part[k];
		if (p->life <= 0) continue;
		p->life -= dt;
		p->x += p->vx * dt;
		p->y += p->vy * dt;
		p->z += p->vz * dt;
		p->vy -= 4.0f * dt;
		if (p->y < 0.05f) {
			p->y = 0.05f;
			p->vy = 0;
		}
	}
}

// ---------------------------------------------------------------------------
// fisica de um carro com entradas: acel (0..1), freio (0..1), direcao (-1..1), derrapar
static void drive(Car* c, float dt, float gas, float brake, float steerIn, bool driftBtn) {
	c->steer += (steerIn - c->steer) * MIN(1.0f, dt * (c->ai ? 6 : 5.5f));
	float v = c->speed;
	bool onRoad = fabsf(c->lat) < RT_HALF_W + RT_CURB_W * 0.8f;
	c->offroad = !onRoad;
	float vmax = (onRoad ? VMAX : V_OFF) + (c->boost > 0 ? VBOOST : 0);
	if (c->ai) vmax *= c->skill;
	// aceleracao
	float acc = 0;
	if (gas > 0 && v >= -0.5f) {
		float k = v / vmax;
		acc = gas * (c->boost > 0 ? 30.0f : (13.5f * (1 - k * k) + 1.5f));
		if (v > vmax) acc = -8.0f * (v - vmax) / 4;
	}
	if (brake > 0) {
		if (v > 0.5f) acc -= 27.0f * brake;
		else acc -= 7.0f * brake;  // re
	}
	// arrasto
	acc -= (v > 0 ? 1 : -1) * (1.5f + 0.0028f * v * v);
	if (!onRoad && v > V_OFF) acc -= 10.0f;
	if (c->drifting) acc -= 1.2f;
	float nv = v + acc * dt;
	if (gas <= 0 && brake <= 0 && fabsf(nv) < 0.3f) nv = 0;
	if (v > 0 && nv < 0 && brake <= 0) nv = 0;
	if (nv < -8) nv = -8;
	c->braking = brake > 0 && v > 1;
	// giro
	float yaw;
	float sp = fabsf(nv);
	float grip = MIN(1.0f, sp / 9.0f);
	if (c->drifting) {
		float into = c->steer * c->driftDir;  // >0 fecha a curva
		yaw = c->driftDir * (0.95f + 0.55f * into) * grip * (sp / (sp + 8.0f)) * 1.6f;
		c->charge += dt * (0.8f + 0.5f * MAX(0.0f, into)) * (onRoad ? 1 : 0.4f);  // na grama carrega devagar
		c->slip += (c->driftDir * 0.34f - c->slip) * MIN(1.0f, dt * 6);
	} else {
		yaw = c->steer * 2.55f * grip / (1.0f + sp / 34.0f);
		c->slip += (0 - c->slip) * MIN(1.0f, dt * 8);
	}
	if (nv < 0) yaw = -yaw;
	c->h = rfWrap(c->h + yaw * dt);
	// derrapagem (jogador): comeca com R/L + direcao
	if (!c->ai) {
		if (driftBtn && !c->drifting && fabsf(c->steer) > 0.35f && nv > 14 && c->hop <= 0.01f) {
			c->drifting = true;
			c->driftDir = c->steer > 0 ? 1 : -1;
			c->charge = 0;
			c->hopV = 3.2f;
		}
		if (c->drifting && (!driftBtn || nv < 10)) {
			c->drifting = false;
			if (c->charge >= DRIFT_MAX) c->boost = MAX(c->boost, 1.5f);
			else if (c->charge >= DRIFT_MIN) c->boost = MAX(c->boost, 0.8f);
			if (c->charge >= DRIFT_MIN) raBeep(1400, 4, 200);
			c->charge = 0;
		}
	}
	// pulinho
	c->hop += c->hopV * dt;
	c->hopV -= 22.0f * dt;
	if (c->hop < 0) {
		c->hop = 0;
		c->hopV = 0;
	}
	c->speed = nv;
	c->x += sinf(c->h) * nv * dt;
	c->z += -cosf(c->h) * nv * dt;
	if (c->boost > 0) c->boost -= dt;
	if (c->boostPad > 0) c->boostPad -= dt;
	if (c->bump > 0) c->bump -= dt;
	// inclinacoes visuais
	float rollT = -c->steer * MIN(1.0f, sp / 30.0f) * 0.07f - c->slip * 0.12f;
	c->roll += (rollT - c->roll) * MIN(1.0f, dt * 6);
	float pitchT = -acc * 0.0022f;
	c->pitch += (CLAMP(pitchT, -0.05f, 0.05f) - c->pitch) * MIN(1.0f, dt * 5);
	// marchas (para o som)
	static const float GEAR[7] = {0, 12, 21, 29, 37, 45, 70};
	float av = fabsf(nv);
	int g = 1;
	while (g < 6 && av > GEAR[g]) g++;
	c->gear = g;
	c->rpm = 0.25f + 0.75f * (av - GEAR[g - 1]) / (GEAR[g] - GEAR[g - 1]);
	if (c->rpm > 1) c->rpm = 1;
}

// paredes invisiveis na borda da area de escape
static void walls(Car* c) {
	rgProject(c);
	if (fabsf(c->lat) > WALL) {
		float sx, sz, rx, rz;
		rgSecPos(c->seg, &sx, &sz);
		rgSecRight(c->seg, &rx, &rz);
		float over = fabsf(c->lat) - WALL;
		float sg = c->lat > 0 ? 1 : -1;
		c->x -= rx * over * sg;
		c->z -= rz * over * sg;
		float th = secHeading(c->seg);
		float rel = rfWrap(c->h - th);
		if (fabsf(rel) < 1.57f) c->h = rfWrap(th + rel * 0.35f);
		if (c->bump <= 0 && c->speed > 6) {
			c->speed *= 0.72f;
			c->bump = 0.4f;
			if (!c->ai) {
				raBump();
				R.shake = 0.5f;
			}
		}
		c->drifting = false;
		rgProject(c);
	}
}

static void collide(void) {
	for (int a = 0; a < R.nCars; a++) {
		for (int b = a + 1; b < R.nCars; b++) {
			Car* A = &R.car[a];
			Car* B = &R.car[b];
			float dx = B->x - A->x, dz = B->z - A->z;
			float d2 = dx * dx + dz * dz;
			const float MIN_D = 2.5f;
			if (d2 >= MIN_D * MIN_D || d2 < 1e-4f) continue;
			float d = sqrtf(d2);
			float push = (MIN_D - d) * 0.5f;
			float nx = dx / d, nz = dz / d;
			A->x -= nx * push;
			A->z -= nz * push;
			B->x += nx * push;
			B->z += nz * push;
			// o carro de tras perde velocidade, o da frente ganha um pouco
			float fa = sinf(A->h) * nx - cosf(A->h) * nz;  // B esta a frente de A?
			if (fa > 0.3f) {
				float dv = (A->speed - B->speed) * 0.35f;
				if (dv > 0) {
					A->speed -= dv;
					B->speed += dv * 0.6f;
				}
			} else if (fa < -0.3f) {
				float dv = (B->speed - A->speed) * 0.35f;
				if (dv > 0) {
					B->speed -= dv;
					A->speed += dv * 0.6f;
				}
			}
			if ((a == 0 || b == 0) && R.car[0].bump <= 0) {
				raBump();
				R.car[0].bump = 0.35f;
				R.shake = 0.3f;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// IA
static void aiDrive(Car* c, float dt) {
	int la = 2 + (int)(c->speed * 0.55f / RT_SEG_LEN);
	int ts = (c->seg + la) % RT_N;
	float tx, tz, rx, rz;
	rgSecPos(ts, &tx, &tz);
	rgSecRight(ts, &rx, &rz);
	float lat = g_rt[ts].line * 0.1f + c->laneBias;
	// desvia de quem esta logo a frente
	for (int k = 0; k < R.nCars; k++) {
		Car* o = &R.car[k];
		if (o == c) continue;
		float ds = o->s - c->s;
		if (ds < -RT_LEN / 2) ds += RT_LEN;
		if (ds > RT_LEN / 2) ds -= RT_LEN;
		if (ds > 0 && ds < 14 && fabsf(o->lat - c->lat) < 2.8f && o->speed < c->speed + 2) {
			lat = o->lat + (o->lat > 0 ? -3.4f : 3.4f);
		}
	}
	lat = CLAMP(lat, -RT_HALF_W + 1.4f, RT_HALF_W - 1.4f);
	tx += rx * lat;
	tz += rz * lat;
	float want = atan2f(tx - c->x, -(tz - c->z));
	float diff = rfWrap(want - c->h);
	float steer = CLAMP(diff * 2.4f, -1, 1);
	// velocidade-alvo: curvatura da pista a frente, habilidade e elastico
	float vt = 1e9f;
	for (int k = 1; k <= 4; k++) vt = MIN(vt, g_rt[(c->seg + la / 2 + k) % RT_N].vt * 0.25f);
	vt *= c->skill;
	Car* p = rgPlayer();
	float gap = c->raceDist - p->raceDist;
	float band = 1.0f;
	if (gap > 60) band = MAX(0.9f, 1.0f - (gap - 60) / 1500.0f);
	if (gap < -40) band = MIN(1.1f, 1.0f + (-gap - 40) / 1200.0f);
	vt *= band;
	float gas = 0, brake = 0;
	if (c->speed < vt) gas = 1;
	else if (c->speed > vt + 2.5f) brake = MIN(1.0f, (c->speed - vt) / 8);
	drive(c, dt, gas, brake, steer, false);
	// usa as setas de turbo e as vezes um turbo de largada
	(void)dt;
}

// ---------------------------------------------------------------------------
static void boostPads(Car* c) {
	if (c->boostPad > 0) return;
	for (int b = 0; b < RT_NBOOST; b++) {
		int si = g_rtBoost[b][0];
		int d = c->seg - si;
		if (d < -RT_N / 2) d += RT_N;
		if (d > RT_N / 2) d -= RT_N;
		if (d < -1 || d > 0) continue;
		if (fabsf(c->lat - g_rtBoost[b][1] * 0.1f) < 2.2f) {
			c->boost = MAX(c->boost, 1.2f);
			c->boostPad = 1.0f;
			if (!c->ai) raBeep(1800, 5, 220);
		}
	}
}

static void laps(Car* c, float prevS) {
	if (prevS > RT_LEN * 0.85f && c->s < RT_LEN * 0.15f) {
		if (c->half) {
			c->lap++;
			c->half = false;
			float lt = R.t - c->lapStart;
			c->lapStart = R.t;
			c->lastLap = lt;
			if (!c->best || lt < c->best) c->best = lt;
			if (c->lap >= R.laps && !c->finished) {
				c->finished = true;
				c->finishTime = R.t;
				R.finishedCount++;
			} else if (!c->ai && c->lap == R.laps - 1) {
				snprintf(R.msg, sizeof(R.msg), "VOLTA FINAL!");
				R.msgT = 2.0f;
				raBeep(1200, 10, 240);
			} else if (!c->ai) {
				snprintf(R.msg, sizeof(R.msg), "VOLTA %d", c->lap + 1);
				R.msgT = 1.4f;
			}
		}
	} else if (prevS < RT_LEN * 0.15f && c->s > RT_LEN * 0.85f) {
		// voltou para tras da linha
		if (c->lap > 0 && !c->half) {
			c->lap--;
			c->half = true;
		}
	}
	if (c->s > RT_LEN * 0.4f && c->s < RT_LEN * 0.6f) c->half = true;
	float d = c->lap * RT_LEN + c->s;
	// no grid (antes de cruzar a linha pela 1a vez) a distancia fica negativa
	if (!c->half && c->s > RT_LEN * 0.6f && c->lap == 0) d = c->s - RT_LEN;
	if (!c->finished) c->raceDist = d;
}

// a esta na frente de b? (quem ja chegou fica na frente, pela ordem de chegada)
static bool ahead(const Car* a, const Car* b) {
	if (a->finished != b->finished) return a->finished;
	if (a->finished) return a->finishTime < b->finishTime;
	return a->raceDist > b->raceDist;
}

static void places(void) {
	for (int k = 0; k < R.nCars; k++) {
		int p = 1;
		for (int j = 0; j < R.nCars; j++)
			if (j != k && ahead(&R.car[j], &R.car[k])) p++;
		R.car[k].place = p;
	}
}

// ---------------------------------------------------------------------------
static void camera(float dt) {
	Car* c = rgPlayer();
	float targetH = c->h + c->slip * 0.55f;
	if (R.state == RS_TITLE || R.state == RS_FINISH) {
		// orbita em volta do carro
		float a = R.stateT * 0.35f + 2.2f;
		float d = R.state == RS_TITLE ? 8.5f : 10.0f;
		R.camX = c->x + sinf(a) * d;
		R.camZ = c->z - cosf(a) * d;
		R.camY = 2.6f;
		R.camTX = c->x;
		R.camTY = 0.9f;
		R.camTZ = c->z;
		R.camH = a + 3.14159f;
		R.fov = 58;
		return;
	}
	float dh = rfWrap(targetH - R.camH);
	R.camH = rfWrap(R.camH + dh * MIN(1.0f, dt * 5.5f));
	float fx = sinf(R.camH), fz = -cosf(R.camH);
	float dist = 6.6f, hgt = 2.55f, look = 4.0f, lookY = 1.15f;
	if (R.camMode == CAM_FAR) {
		dist = 10.0f;
		hgt = 4.0f;
		look = 3.0f;
	} else if (R.camMode == CAM_HOOD) {
		dist = -0.3f;
		hgt = 1.3f;
		look = 20.0f;
		lookY = 1.0f;
	}
	R.camX = c->x - fx * dist;
	R.camZ = c->z - fz * dist;
	R.camY = hgt + c->hop * 0.6f;
	R.camTX = c->x + fx * look;
	R.camTZ = c->z + fz * look;
	R.camTY = lookY + c->hop * 0.4f;
	if (R.shake > 0) {
		R.shake -= dt;
		float s = R.shake * 0.35f;
		R.camX += (frand() - 0.5f) * s;
		R.camY += (frand() - 0.5f) * s;
	}
	if (c->offroad && c->speed > 8) {
		R.camY += (frand() - 0.5f) * 0.06f;
	}
	float fovT = 60 + MAX(0.0f, c->speed) / VMAX * 7 + (c->boost > 0 ? 7 : 0);
	R.fov += (fovT - R.fov) * MIN(1.0f, dt * 4);
}

void rgUpdate(float dt, u32 held, u32 down) {
	R.stateT += dt;
	R.waterT += dt;
	if (R.msgT > 0) R.msgT -= dt;
	updateParticles(dt);
	Car* p = rgPlayer();
	bool racing = R.state == RS_RACE || R.state == RS_FINISH;
	if (R.state == RS_COUNTDOWN) {
		float prev = R.countdown;
		R.countdown -= dt;
		// largada perfeita: apertar A entre 0,6 s e 0,1 s antes do "VAI!"
		if ((down & KEY_A) && R.countdown < 0.6f && R.countdown > 0.05f) R.rocketStart = true;
		if ((down & KEY_A) && R.countdown >= 0.6f && R.countdown < 1.6f) R.rocketStart = false;
		for (int k = 3; k >= 1; k--)
			if (prev > k - 1 && R.countdown <= k - 1 && k > 1) raBeep(880, 8, 230);
		if (R.countdown <= 0) {
			R.state = RS_RACE;
			R.stateT = 0;
			raBeep(1760, 18, 255);
			snprintf(R.msg, sizeof(R.msg), "VAI!");
			R.msgT = 1.0f;
			if (R.rocketStart) {
				p->boost = 1.0f;
				p->speed = 12;
				snprintf(R.msg, sizeof(R.msg), "LARGADA PERFEITA!");
				R.msgT = 1.4f;
			}
			for (int k = 0; k < R.nCars; k++) R.car[k].lapStart = 0;
		}
		// motor acelerando parado
		p->rpm = (held & KEY_A) ? 0.9f : 0.25f;
		camera(dt);
		return;
	}
	if (R.state == RS_TITLE || R.state == RS_PAUSE) {
		camera(dt);
		return;
	}
	if (!racing) return;
	R.t += dt;
	if ((down & KEY_SELECT) && !p->finished) {
		// piloto automatico (demonstracao): voltas com ele ligado nao valem recorde
		R.autopilot = !R.autopilot;
		R.autoUsed = true;
		snprintf(R.msg, sizeof(R.msg), R.autopilot ? "PILOTO AUTOMÃTICO" : "VOCÃ PILOTA!");
		R.msgT = 1.4f;
	}
	for (int k = 0; k < R.nCars; k++) {
		Car* c = &R.car[k];
		float prevS = c->s;
		if (c->ai || c->finished || R.autopilot) {
			if (c->finished && !c->ai && !R.autopilot) {
				// depois da chegada o carro do jogador segue sozinho, devagar
				c->skill = 0.7f;
				aiDrive(c, dt);
			} else {
				aiDrive(c, dt);
			}
		} else {
			float gas = (held & (KEY_A | KEY_Y)) ? 1 : 0;
			float brake = (held & KEY_B) ? 1 : 0;
			float steer = (held & KEY_LEFT) ? -1 : ((held & KEY_RIGHT) ? 1 : 0);
			drive(c, dt, gas, brake, steer, (held & (KEY_R | KEY_L)) != 0);
		}
		walls(c);
		boostPads(c);
		laps(c, prevS);
		// efeitos
		if (c->drifting && (g_frame & 1)) emitDrift(c);
		if (c->boost > 0 && (g_frame & 1)) emitBoost(c);
		if (c->offroad && c->speed > 10 && (g_frame % 3) == 0) {
			u8 fl = g_rt[c->seg].flags;
			emitDust(c, c->lat < 0 ? (fl & 8) : (fl & 16));
		}
	}
	collide();
	places();
	// contramao
	float rel = rfWrap(p->h - secHeading(p->seg));
	static float wrong;
	if (fabsf(rel) > 1.9f && p->speed > 3) wrong += dt;
	else wrong = 0;
	if (wrong > 1.2f && R.msgT <= 0) {
		snprintf(R.msg, sizeof(R.msg), "CONTRAM\xC3\x83O!");
		R.msgT = 1.0f;
	}
	if (p->finished && R.state == RS_RACE) {
		R.state = RS_FINISH;
		R.stateT = 0;
		snprintf(R.msg, sizeof(R.msg), "CHEGADA!");
		R.msgT = 3.0f;
		raBeep(1320, 30, 255);
	}
	camera(dt);
}

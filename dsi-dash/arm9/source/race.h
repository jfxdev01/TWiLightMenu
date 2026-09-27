// DSi Dash Racing — definicoes compartilhadas do jogo de corrida
#pragma once
#include "common.h"
#include "race_data.h"

#define NCARS 6
#define MAX_PART 48

// coordenadas: x leste, y cima, z sul. Rumo h (rad): 0 = norte (-z), cresce no sentido horario.
// frente = (sin h, -cos h); direita = (cos h, sin h)

typedef struct Car {
	float x, z, h;        // posicao (m) e rumo
	float speed;          // m/s (negativo = re)
	float slip;           // angulo visual de derrapagem (rad)
	float steer;          // -1..1 suavizado
	float hop;            // altura do pulinho (m)
	float hopV;
	float roll, pitch;    // inclinacao visual da carroceria
	int seg;              // secao mais proxima
	float lat;            // deslocamento lateral (m, + = direita)
	float s;              // distancia percorrida na volta (m)
	int lap;              // voltas completas
	bool half;            // passou da metade (validacao da volta)
	float raceDist;
	int place;
	float lapStart, lastLap, best;
	bool finished;
	float finishTime;
	bool drifting;
	int driftDir;
	float charge;         // carga do mini-turbo (s)
	float boost;          // turbo restante (s)
	float boostPad;       // cooldown das setas
	bool offroad;
	bool braking;
	u16 color;
	bool ai;
	float skill;
	float laneBias;       // preferencia de faixa da IA
	float bump;
	float rpm;            // 0..1 (som)
	int gear;
} Car;

typedef struct Part {
	float x, y, z, vx, vy, vz;
	float life, maxLife, size;
	u16 color;
	u8 alpha;
} Part;

enum { RS_TITLE, RS_COUNTDOWN, RS_RACE, RS_FINISH, RS_PAUSE };
enum { CAM_CHASE, CAM_FAR, CAM_HOOD, CAM_COUNT };

typedef struct Race {
	Car car[NCARS];
	int nCars;
	int laps;
	int difficulty;       // 0 facil, 1 normal, 2 dificil
	int playerColor;
	int state, pausedFrom;
	float t;              // tempo da corrida
	float countdown;
	float stateT;         // tempo no estado atual
	int camMode;
	float camX, camY, camZ, camTX, camTY, camTZ;  // camera e alvo suavizados
	float camH;           // rumo da camera
	float fov;
	float shake;
	float waterT;
	Part part[MAX_PART];
	bool rocketStart;
	float msgT;
	char msg[48];
	int lastPlace;
	float bestLapEver;
	int finishedCount;
	bool autopilot;       // SELECT: o computador pilota o carro do jogador
	bool autoUsed;        // piloto automatico foi usado nesta corrida
} Race;

extern Race R;

// race_world.c (3D)
void rwInit(void);           // liga o 3D (tela de cima) e carrega texturas
void rwShutdown(void);       // volta ao modo 2D do DSi Dash
void rwRender(void);         // desenha a cena e da glFlush
int  rwPolyCount(void);

// race_game.c (logica)
void rgReset(void);           // monta o grid de largada
void rgUpdate(float dt, u32 held, u32 down);
void rgProject(Car* c);       // atualiza seg/lat/s a partir de x,z
void rgSecPos(int i, float* x, float* z);
void rgSecRight(int i, float* rx, float* rz);
void rgEmit(float x, float y, float z, float vx, float vy, float vz, float life, float size, u16 color, u8 alpha);
Car* rgPlayer(void);

// race_audio.c
void raInit(void);
void raUpdate(void);
void raStop(void);
void raBeep(int hz, int frames, int vol);
void raBump(void);

static inline float rfWrap(float a) {
	while (a > 3.14159265f) a -= 6.2831853f;
	while (a < -3.14159265f) a += 6.2831853f;
	return a;
}

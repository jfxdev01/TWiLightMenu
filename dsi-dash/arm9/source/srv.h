// Servidor HTTP de arquivos (app Transferir)
#pragma once
#include <nds.h>

#define SRV_LOG 6

typedef struct SrvStatus {
	volatile int active;     // 0 parado, 1 recebendo, 2 enviando
	char file[96];
	volatile long done, total;
	volatile int uploads, downloads, requests;
	char log[SRV_LOG][80];
	volatile int logN, version;
} SrvStatus;

bool srvStart(void);
void srvStop(void);
bool srvRunning(void);
int srvPort(void);
const SrvStatus* srvStatus(void);

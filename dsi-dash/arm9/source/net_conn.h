// Conexao TCP/TLS interna (net.c <-> net_tls.c)
#pragma once
#include "net.h"

typedef struct Conn {
	int sock;
	void* tls;
	NetJob* job;
	int timeoutMs;
	bool eof;
	int lowErr;
} Conn;

int sockReadRaw(Conn* c, void* buf, int len);
int sockWriteRaw(Conn* c, const void* buf, int len);

int tlsOpen(Conn* c, const char* host);
int tlsRead(Conn* c, void* buf, int len);
int tlsWrite(Conn* c, const void* buf, int len);
void tlsClose(Conn* c);

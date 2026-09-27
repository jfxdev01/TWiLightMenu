// Protocolo PXI ARM9 <-> ARM7 do DSi Dash (canal PxiChannel_User0).
// Mensagem de 26 bits: [25..22] comando, [21..0] argumento.
// Ponteiros para a RAM principal vao como indice de palavra a partir de 0x02000000.
#pragma once

#define IPC_CMD(m)        (((m) >> 22) & 0xF)
#define IPC_ARG(m)        ((m) & 0x3FFFFF)
#define IPC_MSG(c, a)     ((((unsigned)(c) & 0xF) << 22) | ((unsigned)(a) & 0x3FFFFF))
#define IPC_PTRARG(p)     ((((unsigned)(p)) - 0x02000000u) >> 2)
#define IPC_PTR(a)        (0x02000000u + ((unsigned)(a) << 2))

enum {
	IPC_PING = 0,           // -> 1 = modo DS, 2 = modo DSi
	IPC_BACKLIGHT_SET = 1,  // arg 0..4 (so DSi)
	IPC_BACKLIGHT_GET = 2,  // -> nivel+1, 0 = indisponivel
	IPC_REBOOT_UNLAUNCH = 3,// arg = ptr p/ bloco 0x400 bytes (AutoLoadInfo do Unlaunch)
	IPC_SHUTDOWN = 4,       // desliga (so DSi)
	IPC_CAM = 5,            // camera: arg = (sub << 16) | valor
};

// subcomandos de IPC_CAM
enum {
	CAM7_INIT = 1,    // -> versao do chip (0x2280 = MT9V113)
	CAM7_DEINIT = 2,
	CAM7_SELECT = 3,  // valor: 0 interna, 1 externa, 0xFF nenhuma
	CAM7_SEQ = 4,     // valor: 1 previa, 2 foto
};
#define IPC_CAMARG(sub, v) ((((unsigned)(sub) & 0x3F) << 16) | ((unsigned)(v) & 0xFFFF))

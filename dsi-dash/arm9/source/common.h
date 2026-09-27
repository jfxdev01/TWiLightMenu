// DSi Dash — cabecalho comum do ARM9
#pragma once
#include <nds.h>
#include <calico.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "assets_gen.h"
#include "gfx.h"
#include "ui.h"
#include "sys.h"
#include "net.h"
#include "apps.h"

#define APP_VERSION "2.1"

#define ARRAY_SIZE(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

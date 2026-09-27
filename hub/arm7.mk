BLOCKSDS	?= /opt/blocksds/core

SOURCEDIRS	:= arm7/source

LIBS		:= -ldswifi7 -lnds7 -lc
LIBDIRS		:= $(BLOCKSDS)/libs/dswifi \
		   $(BLOCKSDS)/libs/libnds

include $(BLOCKSDS)/sys/default_makefiles/rom_arm9arm7/Makefile.arm7

BLOCKSDS		?= /opt/blocksds/core
BLOCKSDSEXT		?= /opt/blocksds/external

SOURCEDIRS	:= arm9/source
INCLUDEDIRS	:= arm9/source
BINDIRS		:= assets

LIBS		:= -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -ldswifi9 -lnds9
LIBDIRS		:= $(BLOCKSDS)/libs/dswifi \
		   $(BLOCKSDS)/libs/libnds \
		   $(BLOCKSDSEXT)/mbedtls \
		   $(BLOCKSDSEXT)/libcurl

CXXFLAGS	:= -std=gnu++17

include $(BLOCKSDS)/sys/default_makefiles/rom_arm9arm7/Makefile.arm9

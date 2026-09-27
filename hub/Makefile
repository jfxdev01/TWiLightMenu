#---------------------------------------------------------------------------------
# TWiLight Hub - camera, Wi-Fi and internet utilities for TWiLight Menu++
#
# Unlike the other sub-projects, the Hub is built with BlocksDS instead of
# devkitARM: it needs the DSi-mode WiFi driver (WPA2), the DSi camera driver
# and Mbed TLS/libcurl, which are only available in BlocksDS.
#
#   docker run --rm -v "$PWD":/work -w /work/hub skylyrac/blocksds:dev-v1.24.0 make dist
#
# Packages needed on a native install:
#   wf-pacman -Sy blocksds-toolchain blocksds-mbedtls blocksds-libcurl toolchain-gcc-arm-none-eabi-zlib
#---------------------------------------------------------------------------------

BLOCKSDS	?= /opt/blocksds/core
BLOCKSDSEXT	?= /opt/blocksds/external

export BLOCKSDS
export BLOCKSDSEXT

TARGET		:= hub
GAME_TITLE	:= TWiLight Hub
GAME_SUBTITLE	:= Camera, Wi-Fi & Internet
GAME_AUTHOR	:= TWiLight Menu++
GAME_ICON	:= icon.bmp

MAKE		:= make

ifeq ($(VERBOSE),1)
V		:=
else
V		:= @
endif

.PHONY: all clean arm9 arm7 dist sim

all: $(TARGET).nds

arm9:
	$(V)+$(MAKE) -f arm9.mk --no-print-directory

arm7:
	$(V)+$(MAKE) -f arm7.mk --no-print-directory

# Same title ID and game code as the rest of TWiLight Menu++'s .srldr files, so
# it gets the same access rights when launched from the menu.
$(TARGET).nds: arm9 arm7
	@echo "  NDSTOOL $@"
	$(V)$(BLOCKSDS)/tools/ndstool/ndstool -c $@ \
		-7 build/arm7.elf -9 build/arm9.elf \
		-u 00030004 -g SRLA 01 "TWLMENUPP" \
		-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE);$(GAME_AUTHOR)"

dist: all
	@mkdir -p ../7zfile/_nds/TWiLightMenu ../7zfile/debug
	@cp $(TARGET).nds ../7zfile/_nds/TWiLightMenu/hub.srldr
	@cp build/arm9.elf ../7zfile/debug/$(TARGET).arm9.elf
	@cp build/arm7.elf ../7zfile/debug/$(TARGET).arm7.elf

# Host build that renders every screen to PNG files (see tools/sim).
sim:
	$(V)$(MAKE) -C tools/sim --no-print-directory

clean:
	@echo "  CLEAN"
	$(V)$(MAKE) -f arm9.mk clean --no-print-directory
	$(V)$(MAKE) -f arm7.mk clean --no-print-directory
	$(V)rm -rf $(TARGET).nds build

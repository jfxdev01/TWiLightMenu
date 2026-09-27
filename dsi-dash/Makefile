#---------------------------------------------------------------------------------
# DSi Dash — launcher para Nintendo DSi (ARM7 + ARM9)
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

export TARGET := dsidash
export TOPDIR := $(CURDIR)

GAME_ICON      := $(CURDIR)/icon.bmp
GAME_TITLE     := DSi Dash
GAME_SUBTITLE1 := Launcher Wi-Fi para DSi
GAME_SUBTITLE2 := v2.1

NITRO_FILES :=

include $(DEVKITARM)/ds_rules

.PHONY: checkarm7 checkarm9 clean

all: checkarm7 checkarm9 $(TARGET).nds

checkarm7:
	$(MAKE) -C arm7

checkarm9:
	$(MAKE) -C arm9

$(TARGET).nds : $(NITRO_FILES) arm7/$(TARGET).elf arm9/$(TARGET).elf
	ndstool	-c $(TARGET).nds -7 arm7/$(TARGET).elf -9 arm9/$(TARGET).elf \
	-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1);$(GAME_SUBTITLE2)" \
	$(_ADDFILES)

arm7/$(TARGET).elf:
	$(MAKE) -C arm7

arm9/$(TARGET).elf:
	$(MAKE) -C arm9

clean:
	$(MAKE) -C arm9 clean
	$(MAKE) -C arm7 clean
	rm -f $(TARGET).nds

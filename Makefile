# Sigil Siege - Nintendo 3DS build
#   make        -> SigilSiege.3dsx   (Homebrew Launcher)
#   make cia    -> SigilSiege.cia    (installable with FBI; needs makerom + bannertool on PATH)
#
# Requires devkitPro with devkitARM + the 3ds-dev package group (libctru, citro2d, citro3d).

ifneq ($(MAKECMDGOALS),test)
ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO first, e.g.  export DEVKITPRO=/opt/devkitpro)
endif
endif
DEVKITARM ?= $(DEVKITPRO)/devkitARM
export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITARM)/bin:$(PATH)

TARGET     := SigilSiege
BUILD      := build
SRC        := source
APP_TITLE  := Sigil Siege
APP_DESC   := Draw runes to slay the horde
APP_AUTHOR := Homebrew

CC     := arm-none-eabi-gcc
ARCH   := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH) \
          -D__3DS__ -std=gnu11 \
          -I$(DEVKITPRO)/libctru/include -I$(DEVKITPRO)/portlibs/3ds/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections \
           -L$(DEVKITPRO)/libctru/lib -L$(DEVKITPRO)/portlibs/3ds/lib
LIBS   := -lcitro2d -lcitro3d -lctru -lm

SOURCES := $(wildcard $(SRC)/*.c)
OBJECTS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES))

.PHONY: all cia clean test
all: $(TARGET).3dsx
cia: $(TARGET).cia

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: $(SRC)/%.c $(wildcard $(SRC)/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@

$(BUILD)/$(TARGET).smdh: resources/icon.png | $(BUILD)
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" resources/icon.png $@

$(TARGET).3dsx: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).smdh
	3dsxtool $< $@ --smdh=$(BUILD)/$(TARGET).smdh

$(BUILD)/icon.icn: resources/icon.png | $(BUILD)
	bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESC)" -p "$(APP_AUTHOR)" -i resources/icon.png -o $@

$(BUILD)/banner.bnr: resources/banner.png resources/audio.wav | $(BUILD)
	bannertool makebanner -i resources/banner.png -a resources/audio.wav -o $@

$(TARGET).cia: $(BUILD)/$(TARGET).elf $(BUILD)/icon.icn $(BUILD)/banner.bnr resources/cia.rsf
	makerom -f cia -o $@ -elf $(BUILD)/$(TARGET).elf -rsf resources/cia.rsf \
		-icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr -exefslogo -target t

# Host-side unit tests (recognizer + wave logic); runs on a normal PC.
test:
	@mkdir -p $(BUILD)
	gcc -O2 -Wall -Wextra -std=gnu11 -o $(BUILD)/host_test tests/host_test.c $(SRC)/shapes.c $(SRC)/game.c -lm
	$(BUILD)/host_test

clean:
	rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).cia

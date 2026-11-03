.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*LOOPER_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
# MOD=1: the separate Mod on Daisy build (docs/MODULATION.md), own image and build
# directory; the default build compiles exactly the sources without the matrix.
MOD ?= 0
MOD_ON_DAISY = ../../modules/mod-on-daisy
MOD_DEFS = -DLOOPER_MOD=1 -DMOD_ON_DAISY_EXTENSIONS=1
MOD_FLAGS = $(MOD_DEFS) -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
ifeq ($(MOD),1)
TARGET = LooperMod-v$(VERSION)
override BUILD_DIR = build/mod
C_DEFS = $(MOD_DEFS)
C_INCLUDES += -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
else
TARGET = Looper-v$(VERSION)
endif
STORE_SOURCES = src/wav.cpp src/resample.cpp src/loop_store.cpp
CPP_SOURCES = src/main.cpp src/looper_engine.cpp src/midi_control.cpp $(STORE_SOURCES) \
	src/fat_volume.cpp src/fatfs_volume.cpp
# One FatFs only: common/sd's R0.16 (exFAT, LFN) in its writable configuration; libDaisy's
# R0.12c, FatFSInterface and SD_Driver are not linked (make sd-check).
USE_FATFS = 0
SD_WRITABLE = 1
SD_DIR = ../../common/sd
BUILD_DIR ?= build
include $(SD_DIR)/sd.mk
C_INCLUDES += -Isrc -I$(SD_DIR)/reader
include ../../common/desktop.mk

.PHONY: test test-sanitize test-card version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
CALLBACK_FLAGS = -Isrc -Itests/fakes -I$(BOOTLOADER_DIR) -I$(LIBDAISY_DIR)/src -DLOOPER_BUFFER_FRAMES=192000
CALLBACK_SOURCES = src/looper_engine.cpp src/midi_control.cpp $(STORE_SOURCES)
# The callback tests run the production main.cpp with a 4 s buffer and a card in memory,
# the MOD variants with the Mod on Daisy matrix. Both builds always run, whatever MOD is.
HOST_CC ?= gcc
SAN ?=
CARD_OPT ?= -O2
C = build/host/card
SD_FAKES = -I$(SD_DIR)/tests/fakes
# The card stack as on the board (FatVolume + writable common/sd + FatFs R0.16) on FAT32
# and exFAT images, including a power-loss campaign over every block of a save.
test-card:
	mkdir -p $(C)
	$(HOST_CC) -std=gnu11 $(CARD_OPT) $(SAN) -I$(SD_DIR)/tests/mkfs -I$(SD_DIR)/fatfs $(SD_DIR)/tests/mkimage.c \
		$(SD_DIR)/fatfs/ff.c $(SD_DIR)/fatfs/ffunicode.c -o $(C)/mkimage
	./$(C)/mkimage fat32 $(C)/fat32.img
	./$(C)/mkimage exfat $(C)/exfat.img
	./$(C)/mkimage exfat-32k $(C)/exfat-32k.img
	$(HOST_CC) -std=gnu11 $(CARD_OPT) $(SAN) -I$(SD_DIR)/rw -I$(SD_DIR) -I$(SD_DIR)/fatfs -c $(SD_DIR)/fatfs/ff.c -o $(C)/ff.o
	$(HOST_CC) -std=gnu11 $(CARD_OPT) $(SAN) -I$(SD_DIR)/rw -I$(SD_DIR) -I$(SD_DIR)/fatfs -c $(SD_DIR)/fatfs/ffunicode.c -o $(C)/ffunicode.o
	$(HOST_CC) -std=gnu11 $(CARD_OPT) $(SAN) -Wall -Wextra -Werror -I$(SD_DIR)/rw -I$(SD_DIR) -I$(SD_DIR)/fatfs \
		$(SD_FAKES) -c $(SD_DIR)/ro_sd.c -o $(C)/ro_sd.o
	$(HOST_CXX) $(HOST_FLAGS) -Isrc -I$(SD_DIR)/rw -I$(SD_DIR) -I$(SD_DIR)/fatfs $(SD_FAKES) tests/test_card.cpp \
		src/fat_volume.cpp src/looper_engine.cpp $(STORE_SOURCES) $(C)/ro_sd.o $(C)/ff.o $(C)/ffunicode.o -o $(C)/test_card
	./$(C)/test_card $(C)/fat32.img $(C)/exfat.img $(C)/exfat-32k.img

test: test-card
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_engine.cpp src/looper_engine.cpp -o build/host/test_engine
	./build/host/test_engine
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_wav.cpp src/wav.cpp -o build/host/test_wav
	./build/host/test_wav
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_resample.cpp src/resample.cpp -o build/host/test_resample
	./build/host/test_resample
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_midi.cpp src/midi_control.cpp -o build/host/test_midi
	./build/host/test_midi
	$(HOST_CXX) $(HOST_FLAGS) -Isrc -Itests/fakes tests/test_store.cpp src/looper_engine.cpp $(STORE_SOURCES) -o build/host/test_store
	./build/host/test_store
	$(HOST_CXX) $(HOST_FLAGS) $(CALLBACK_FLAGS) tests/test_callback.cpp $(CALLBACK_SOURCES) -o build/host/test_callback
	./build/host/test_callback
	$(HOST_CXX) $(HOST_FLAGS) $(CALLBACK_FLAGS) $(MOD_FLAGS) tests/test_callback.cpp $(CALLBACK_SOURCES) -o build/host/test_callback_mod
	./build/host/test_callback_mod
	$(HOST_CXX) $(HOST_FLAGS) $(MOD_FLAGS) -Isrc tests/test_columns.cpp src/looper_engine.cpp -o build/host/test_columns
	./build/host/test_columns
	$(HOST_CXX) $(HOST_FLAGS) $(CALLBACK_FLAGS) $(MOD_FLAGS) tests/test_modulation.cpp $(CALLBACK_SOURCES) -o build/host/test_modulation
	./build/host/test_modulation
	$(HOST_CXX) $(HOST_FLAGS) $(CALLBACK_FLAGS) tests/test_golden.cpp $(CALLBACK_SOURCES) -o build/host/test_golden
	$(HOST_CXX) $(HOST_FLAGS) $(CALLBACK_FLAGS) $(MOD_FLAGS) tests/test_golden.cpp $(CALLBACK_SOURCES) -o build/host/test_golden_mod
	./build/host/test_golden > build/host/golden.txt
	./build/host/test_golden_mod > build/host/golden_mod.txt
	cat build/host/golden.txt build/host/golden_mod.txt
	cmp build/host/golden.txt build/host/golden_mod.txt
	@echo "PASS golden: the MOD build with an empty matrix sounds bit-identical to the regular build"

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" \
		CARD_OPT="-O1 -g" SAN="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

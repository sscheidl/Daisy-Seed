.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*NEBULAE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
TARGET = NebulaeOnDaisy-v$(VERSION)
SD_DIR = ../../common/sd
READER_DIR = $(SD_DIR)/reader
PROTOCOL_DIR = ../../diagnostics/protocol
RING_DIR = ../../diagnostics/firmware/src
DSP_SOURCES = src/reference_dsp.cpp src/control.cpp src/engine.cpp src/wav.cpp
# Performance personality (host-tested through test_callback) + SD management.
CPP_SOURCES = src/main.cpp src/hw_glue.cpp src/admin.cpp src/banks.cpp src/sd_files.cpp $(DSP_SOURCES) \
	$(READER_DIR)/card_reader.cpp $(READER_DIR)/msc_bot.cpp $(READER_DIR)/reader_usb.cpp \
	$(PROTOCOL_DIR)/ddp.cpp
C_SOURCES = src/heap.c $(READER_DIR)/reader_ctlreq.c
# One FatFs only: common/sd's read-only R0.16 (exFAT, LFN). libDaisy's R0.12c,
# FatFSInterface and SD_Driver are not linked (make sd-check).
USE_FATFS = 0
# libDaisy default, set early: sd.mk names the ELF in its sd-check rule.
BUILD_DIR = build
include $(SD_DIR)/sd.mk
C_INCLUDES += -Isrc -I$(READER_DIR) -I$(PROTOCOL_DIR) -I$(RING_DIR) -fstack-usage
C_INCLUDES += -I../../libDaisy/Middlewares/Patched/ST/STM32_USB_Device_Library/Class/CDC/Inc
# USB identity of the SD management composite (CDC + MSC): its own Windows instance.
C_DEFS += -DREADER_USB_PRODUCT='"Nebulae on Daisy SD"' -DREADER_USB_BCD_MAJOR=2 -DREADER_USB_SERIAL_SUFFIX="'N'"
include ../../common/desktop.mk
LDFLAGS += -Tsrc/memory.ld
$(BUILD_DIR)/$(TARGET).elf: src/memory.ld
all: $(BUILD_DIR)/SHA256SUMS
$(BUILD_DIR)/SHA256SUMS: $(BUILD_DIR)/$(TARGET).bin
	cd $(BUILD_DIR) && sha256sum $(TARGET).bin > SHA256SUMS
# MSC may write single blocks with IDMA (the card reader's hardware-tested path); every
# FatFs mutation, erase, lock, PIO/IT writes and multi-buffer DMA stay banned.
SD_CHECK_ALLOW = SDMMC_CmdWriteSingleBlock,SDMMC_CmdWriteMultiBlock,HAL_SD_WriteBlocks_DMA
SD_CHECK_REQUIRE = disk_read,f_mount,f_readdir,f_opendir,SDMMC_CmdReadSingleBlock,HAL_SD_WriteBlocks_DMA

HOST_CXX ?= g++
HOST_CC ?= gcc
HOST_FLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -pthread
HOST_CFLAGS ?= -std=gnu11 -O2
H = build/host
SD_FAKES = -I$(SD_DIR)/tests/fakes
.PHONY: version test test-sanitize test-thread e2e
version:
	@echo $(VERSION)
# Simulated Share -> Windows write -> Eject -> Return -> Rescan, FAT32 and exFAT.
# Not a substitute for the hardware test.
e2e:
	mkdir -p $(H)/e2e
	$(HOST_CC) $(HOST_CFLAGS) $(SAN) -I$(SD_DIR)/tests/mkfs -I$(SD_DIR)/fatfs $(SD_DIR)/tests/mkimage.c \
		$(SD_DIR)/fatfs/ff.c $(SD_DIR)/fatfs/ffunicode.c -o $(H)/e2e/mkimage
	$(HOST_CC) $(HOST_CFLAGS) $(SAN) -I$(SD_DIR)/tests/mkfs -I$(SD_DIR)/fatfs -c $(SD_DIR)/fatfs/ff.c -o $(H)/e2e/ff_rw.o
	$(HOST_CC) $(HOST_CFLAGS) $(SAN) -I$(SD_DIR)/tests/mkfs -I$(SD_DIR)/fatfs -c $(SD_DIR)/fatfs/ffunicode.c -o $(H)/e2e/ffu_rw.o
	$(HOST_CXX) $(HOST_FLAGS) -I$(READER_DIR) -I$(SD_DIR)/tests/mkfs -I$(SD_DIR)/fatfs tests/e2e_windows.cpp \
		$(READER_DIR)/card_reader.cpp $(READER_DIR)/msc_bot.cpp $(H)/e2e/ff_rw.o $(H)/e2e/ffu_rw.o -o $(H)/e2e/e2e_windows
	$(HOST_CC) $(HOST_CFLAGS) $(SAN) -I$(SD_DIR) -I$(SD_DIR)/fatfs -c $(SD_DIR)/fatfs/ff.c -o $(H)/e2e/ff_ro.o
	$(HOST_CC) $(HOST_CFLAGS) $(SAN) -I$(SD_DIR) -I$(SD_DIR)/fatfs -c $(SD_DIR)/fatfs/ffunicode.c -o $(H)/e2e/ffu_ro.o
	$(HOST_CC) $(HOST_CFLAGS) $(SAN) -Wall -Wextra -Werror -I$(SD_DIR) -I$(SD_DIR)/fatfs $(SD_FAKES) \
		-c $(SD_DIR)/ro_sd.c -o $(H)/e2e/ro_sd.o
	$(HOST_CXX) $(HOST_FLAGS) -Isrc -I$(SD_DIR) -I$(SD_DIR)/fatfs $(SD_FAKES) tests/test_e2e_return.cpp \
		src/banks.cpp src/sd_files.cpp src/wav.cpp $(H)/e2e/ro_sd.o $(H)/e2e/ff_ro.o $(H)/e2e/ffu_ro.o \
		-o $(H)/e2e/test_e2e_return
	$(H)/e2e/mkimage fat32 $(H)/e2e/fat32.img
	$(H)/e2e/mkimage exfat $(H)/e2e/exfat.img
	$(H)/e2e/e2e_windows $(H)/e2e/fat32.img $(H)/e2e/fat32-after.img $(H)/e2e/fat32-expected.txt
	$(H)/e2e/test_e2e_return $(H)/e2e/fat32-after.img $(H)/e2e/fat32-expected.txt
	$(H)/e2e/e2e_windows $(H)/e2e/exfat.img $(H)/e2e/exfat-after.img $(H)/e2e/exfat-expected.txt
	$(H)/e2e/test_e2e_return $(H)/e2e/exfat-after.img $(H)/e2e/exfat-expected.txt
test: e2e
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_nebulae.cpp $(DSP_SOURCES) -o $(H)/test_nebulae
	./$(H)/test_nebulae
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_banks.cpp src/banks.cpp -o $(H)/test_banks
	./$(H)/test_banks
	$(HOST_CXX) $(HOST_FLAGS) -Isrc -I$(BOOTLOADER_DIR) tests/test_control_sysex.cpp src/control.cpp -o $(H)/test_control_sysex
	./$(H)/test_control_sysex
	$(HOST_CXX) $(HOST_FLAGS) -Isrc -I$(READER_DIR) -I$(PROTOCOL_DIR) -I$(RING_DIR) tests/test_admin.cpp src/admin.cpp \
		$(READER_DIR)/card_reader.cpp $(PROTOCOL_DIR)/ddp.cpp -o $(H)/test_admin
	./$(H)/test_admin
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(SD_FAKES) -Isrc -I$(BOOTLOADER_DIR) -I../../libDaisy/src -I$(SD_DIR) \
		-I$(SD_DIR)/fatfs tests/test_callback.cpp $(DSP_SOURCES) src/banks.cpp src/sd_files.cpp \
		$(H)/e2e/ro_sd.o $(H)/e2e/ff_ro.o $(H)/e2e/ffu_ro.o -o $(H)/test_callback
	./$(H)/test_callback $(H)/e2e/fat32-after.img
	./$(H)/test_callback $(H)/e2e/exfat-after.img
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_queue.cpp -o $(H)/test_queue
	./$(H)/test_queue
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_bridge.cpp $(DSP_SOURCES) -o $(H)/test_bridge
	./$(H)/test_bridge
SANITIZE = -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer
test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread $(SANITIZE)" \
		HOST_CFLAGS="-std=gnu11 -O1 -g" SAN="$(SANITIZE)"
test-thread:
	mkdir -p $(H)
	$(HOST_CXX) -std=c++17 -O1 -g -pthread -fsanitize=thread -Isrc tests/test_queue.cpp -o $(H)/test_queue
	./$(H)/test_queue
	$(HOST_CXX) -std=c++17 -O1 -g -pthread -fsanitize=thread -Isrc tests/test_bridge.cpp $(DSP_SOURCES) -o $(H)/test_bridge
	./$(H)/test_bridge
	$(HOST_CXX) -std=c++17 -O1 -g -pthread -fsanitize=thread -Isrc -I$(BOOTLOADER_DIR) tests/test_sysex_thread.cpp \
		src/control.cpp -o $(H)/test_sysex_thread
	./$(H)/test_sysex_thread

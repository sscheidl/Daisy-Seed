.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*NEBULAE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
# MOD=1: the separate Mod on Daisy build (docs/MODULATION.md), own image and build
# directory; the default build compiles exactly the sources without the matrix.
MOD ?= 0
MOD_ON_DAISY = ../../modules/mod-on-daisy
MOD_DEFS = -DNEBULAE_MOD=1 -DMOD_ON_DAISY_EXTENSIONS=1
MOD_FLAGS = $(MOD_DEFS) -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
# PROFILE=1: diagnostic image with DWT cycle splits, budget guard, boot benchmark and the
# PROFILE SysEx (docs/OVERLOAD_DIAGNOSIS.md). Never a release; sound is wrong under load.
PROFILE ?= 0
PROFILE_FLAGS = -DNEBULAE_PROFILE=1
ifeq ($(MOD),1)
TARGET := NebulaeMod
VARIANT := mod
C_DEFS = $(MOD_DEFS)
C_INCLUDES += -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
else
TARGET := NebulaeOnDaisy
VARIANT :=
endif
ifeq ($(PROFILE),1)
TARGET := $(TARGET)Profile
VARIANT := $(if $(VARIANT),$(VARIANT)-profile,profile)
C_DEFS += $(PROFILE_FLAGS)
endif
# SCHEDULE=1|2: listening candidates with the vocoder channels on separate blocks
# (engine.h Schedule, docs/OVERLOAD_DIAGNOSIS.md section 11). 0 is the common grid.
SCHEDULE ?= 0
ifeq ($(SCHEDULE),1)
TARGET := $(TARGET)Stagger
VARIANT := $(if $(VARIANT),$(VARIANT)-stagger,stagger)
C_DEFS += -DNEBULAE_VOCODER_SCHEDULE=1
endif
ifeq ($(SCHEDULE),2)
TARGET := $(TARGET)Pipeline
VARIANT := $(if $(VARIANT),$(VARIANT)-pipeline,pipeline)
C_DEFS += -DNEBULAE_VOCODER_SCHEDULE=2
endif
TARGET := $(TARGET)-v$(VERSION)
SD_DIR = ../../common/sd
READER_DIR = $(SD_DIR)/reader
PROTOCOL_DIR = ../../diagnostics/protocol
RING_DIR = ../../diagnostics/firmware/src
DSP_CORE = src/reference_dsp.cpp src/control.cpp src/engine.cpp src/wav.cpp
# Vocoder FFT: CMSIS-DSP 1.10 from the pinned libDaisy (Apache-2.0), compiled into the
# product; libdaisy.a does not contain it. Only the transforms and tables src/fft_cmsis.cpp
# references survive --gc-sections.
CMSIS_DSP = ../../libDaisy/Drivers/CMSIS-DSP
FFT_C = $(CMSIS_DSP)/Source/TransformFunctions/arm_rfft_fast_f32.c 	$(CMSIS_DSP)/Source/TransformFunctions/arm_cfft_f32.c 	$(CMSIS_DSP)/Source/TransformFunctions/arm_cfft_radix8_f32.c 	$(CMSIS_DSP)/Source/TransformFunctions/arm_bitreversal2.c 	$(CMSIS_DSP)/Source/CommonTables/arm_common_tables.c
# Performance personality (host-tested through test_callback) + SD management.
CPP_SOURCES = src/main.cpp src/hw_glue.cpp src/admin.cpp src/banks.cpp src/sd_files.cpp $(DSP_CORE) src/fft_cmsis.cpp \
	$(READER_DIR)/card_reader.cpp $(READER_DIR)/msc_bot.cpp $(READER_DIR)/reader_usb.cpp \
	$(PROTOCOL_DIR)/ddp.cpp
C_SOURCES = src/heap.c $(READER_DIR)/reader_ctlreq.c $(FFT_C)
# One FatFs only: common/sd's read-only R0.16 (exFAT, LFN). libDaisy's R0.12c,
# FatFSInterface and SD_Driver are not linked (make sd-check).
USE_FATFS = 0
# libDaisy default, set early: sd.mk names the ELF in its sd-check rule.
ifneq ($(VARIANT),)
override BUILD_DIR = build/$(VARIANT)
else
BUILD_DIR = build
endif
include $(SD_DIR)/sd.mk
C_INCLUDES += -Isrc -I$(READER_DIR) -I$(PROTOCOL_DIR) -I$(RING_DIR) -fstack-usage
C_INCLUDES += -I$(CMSIS_DSP)/PrivateInclude
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
# FatFs mutation, erase, lock, PIO/IT writes and multi-buffer DMA stay banned, and make
# sd-check injects those writers to prove it.
SD_CHECK_ALLOW = $(SD_READER_ALLOW)
SD_CHECK_PROBES = $(SD_READER_PROBES)
SD_CHECK_REQUIRE = disk_read,f_mount,f_readdir,f_opendir,SDMMC_CmdReadSingleBlock,HAL_SD_WriteBlocks_DMA

HOST_CXX ?= g++
HOST_CC ?= gcc
HOST_FLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -pthread
HOST_CFLAGS ?= -std=gnu11 -O2
H = build/host
# Host build of the same FFT backend: CMSIS-DSP's generic C path (__GNUC_PYTHON__ keeps
# the Cortex core headers out); its headers are system headers for -Werror.
FFT_HOST = -D__GNUC_PYTHON__ -isystem $(CMSIS_DSP)/Include -isystem $(CMSIS_DSP)/PrivateInclude
FFT_OBJS = $(H)/fft/fft_cmsis.o $(patsubst %.c,$(H)/fft/%.o,$(notdir $(FFT_C)))
# What a host test links for the DSP: the sources plus the FFT objects of fft-host.
DSP_SOURCES = $(DSP_CORE) $(FFT_OBJS)
# Host A/B only: the engine around the 0.2.1 vocoder (tests/compare_audio.cpp).
REFERENCE_FLAGS = -DNEBULAE_REFERENCE_VOCODER=1
SD_FAKES = -I$(SD_DIR)/tests/fakes
# The production main.cpp with fakes and the real read-only SD stack (production_harness.h).
PRODUCTION = -Itests/fakes $(SD_FAKES) -Isrc -I$(BOOTLOADER_DIR) -I../../libDaisy/src -I$(SD_DIR) -I$(SD_DIR)/fatfs
PRODUCTION_LINK = $(DSP_SOURCES) src/banks.cpp src/sd_files.cpp $(H)/e2e/ro_sd.o $(H)/e2e/ff_ro.o $(H)/e2e/ffu_ro.o
.PHONY: version test test-sanitize test-thread e2e fft-host
fft-host:
	mkdir -p $(H)/fft
	$(foreach f,$(FFT_C),$(HOST_CC) $(HOST_CFLAGS) $(SAN) $(FFT_HOST) -c $(f) -o $(H)/fft/$(notdir $(f:.c=.o)) &&) true
	$(HOST_CXX) $(HOST_FLAGS) $(FFT_HOST) -Isrc -c src/fft_cmsis.cpp -o $(H)/fft/fft_cmsis.o
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
test: e2e fft-host
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_nebulae.cpp $(DSP_SOURCES) -o $(H)/test_nebulae
	./$(H)/test_nebulae
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_vocoder.cpp $(DSP_SOURCES) -o $(H)/test_vocoder
	./$(H)/test_vocoder
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_schedule.cpp $(DSP_SOURCES) -o $(H)/test_schedule
	./$(H)/test_schedule
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_banks.cpp src/banks.cpp -o $(H)/test_banks
	./$(H)/test_banks
	$(HOST_CXX) $(HOST_FLAGS) -Isrc -I$(BOOTLOADER_DIR) tests/test_control_sysex.cpp src/control.cpp -o $(H)/test_control_sysex
	./$(H)/test_control_sysex
	$(HOST_CXX) $(HOST_FLAGS) $(PROFILE_FLAGS) -Isrc -I$(BOOTLOADER_DIR) tests/test_control_sysex.cpp src/control.cpp -o $(H)/test_control_sysex_profile
	./$(H)/test_control_sysex_profile
	$(HOST_CXX) $(HOST_FLAGS) -Isrc -I$(READER_DIR) -I$(PROTOCOL_DIR) -I$(RING_DIR) tests/test_admin.cpp src/admin.cpp \
		$(READER_DIR)/card_reader.cpp $(PROTOCOL_DIR)/ddp.cpp -o $(H)/test_admin
	./$(H)/test_admin
	$(HOST_CXX) $(HOST_FLAGS) $(PRODUCTION) tests/test_callback.cpp $(PRODUCTION_LINK) -o $(H)/test_callback
	./$(H)/test_callback $(H)/e2e/fat32-after.img
	./$(H)/test_callback $(H)/e2e/exfat-after.img
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_queue.cpp -o $(H)/test_queue
	./$(H)/test_queue
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_bridge.cpp $(DSP_SOURCES) -o $(H)/test_bridge
	./$(H)/test_bridge
	$(HOST_CXX) $(HOST_FLAGS) $(MOD_FLAGS) -Isrc tests/test_mod_bridge.cpp -o $(H)/test_mod_bridge
	./$(H)/test_mod_bridge
	$(HOST_CXX) $(HOST_FLAGS) $(MOD_FLAGS) -Isrc tests/test_mod_thread.cpp -o $(H)/test_mod_thread
	./$(H)/test_mod_thread
	$(HOST_CXX) $(HOST_FLAGS) $(MOD_FLAGS) $(PRODUCTION) tests/test_callback.cpp $(PRODUCTION_LINK) -o $(H)/test_callback_mod
	./$(H)/test_callback_mod $(H)/e2e/fat32-after.img
	./$(H)/test_callback_mod $(H)/e2e/exfat-after.img
	$(HOST_CXX) $(HOST_FLAGS) $(MOD_FLAGS) $(PRODUCTION) tests/test_modulation.cpp $(PRODUCTION_LINK) -o $(H)/test_modulation
	./$(H)/test_modulation $(H)/e2e/fat32-after.img $(H)/e2e/fat32-expected.txt
	./$(H)/test_modulation $(H)/e2e/exfat-after.img $(H)/e2e/exfat-expected.txt
	$(HOST_CXX) $(HOST_FLAGS) $(MOD_FLAGS) $(PROFILE_FLAGS) $(PRODUCTION) tests/test_profile.cpp $(PRODUCTION_LINK) -o $(H)/test_profile
	./$(H)/test_profile $(H)/e2e/fat32-after.img
	$(HOST_CXX) $(HOST_FLAGS) $(PRODUCTION) tests/test_golden.cpp $(PRODUCTION_LINK) -o $(H)/test_golden
	$(HOST_CXX) $(HOST_FLAGS) $(MOD_FLAGS) $(PRODUCTION) tests/test_golden.cpp $(PRODUCTION_LINK) -o $(H)/test_golden_mod
	$(HOST_CXX) $(HOST_FLAGS) $(MOD_FLAGS) $(PROFILE_FLAGS) $(PRODUCTION) tests/test_golden.cpp $(PRODUCTION_LINK) -o $(H)/test_golden_profile
	./$(H)/test_golden $(H)/e2e/fat32-after.img > $(H)/golden.txt
	./$(H)/test_golden_mod $(H)/e2e/fat32-after.img > $(H)/golden_mod.txt
	./$(H)/test_golden_mod $(H)/e2e/fat32-after.img --route > $(H)/golden_route.txt
	./$(H)/test_golden_profile $(H)/e2e/fat32-after.img > $(H)/golden_profile.txt
	$(HOST_CXX) $(HOST_FLAGS) $(REFERENCE_FLAGS) $(PRODUCTION) tests/test_golden.cpp $(PRODUCTION_LINK) -o $(H)/test_golden_reference
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/compare_audio.cpp $(DSP_SOURCES) -o $(H)/compare_audio
	NEBULAE_GOLDEN_DUMP=$(H)/golden.f32 ./$(H)/test_golden $(H)/e2e/fat32-after.img > /dev/null
	NEBULAE_GOLDEN_DUMP=$(H)/golden_reference.f32 ./$(H)/test_golden_reference $(H)/e2e/fat32-after.img > $(H)/golden_reference.txt
	cat $(H)/golden.txt $(H)/golden_mod.txt $(H)/golden_route.txt $(H)/golden_profile.txt
	cmp $(H)/golden.txt $(H)/golden_mod.txt
	cmp $(H)/golden.txt $(H)/golden_profile.txt
	test "$$(cat $(H)/golden.txt)" != "$$(cat $(H)/golden_route.txt)"
	@echo "PASS golden: the MOD build with an empty matrix sounds bit-identical to the regular build, and one active route changes the hash"
	@echo "PASS golden: the profile build sounds bit-identical to the regular build"
	@echo "0.2.1 vocoder in this engine: $$(cat $(H)/golden_reference.txt) (equals the 0.2.1 golden on the same compiler: the rest of the engine is bit-identical)"
	test "$$(cat $(H)/golden.txt)" != "$$(cat $(H)/golden_reference.txt)"
	NEBULAE_GOLDEN_SCHEDULE=1 ./$(H)/test_golden $(H)/e2e/fat32-after.img > $(H)/golden_stagger.txt
	NEBULAE_GOLDEN_SCHEDULE=2 ./$(H)/test_golden $(H)/e2e/fat32-after.img > $(H)/golden_pipeline.txt
	@echo "listening candidates: stagger $$(cat $(H)/golden_stagger.txt), pipeline $$(cat $(H)/golden_pipeline.txt)"
	test "$$(cat $(H)/golden.txt)" != "$$(cat $(H)/golden_stagger.txt)"
	test "$$(cat $(H)/golden.txt)" != "$$(cat $(H)/golden_pipeline.txt)"
	./$(H)/compare_audio $(H)/golden.f32 $(H)/golden_reference.f32
SANITIZE = -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer
test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread $(SANITIZE)" \
		HOST_CFLAGS="-std=gnu11 -O1 -g" SAN="$(SANITIZE)"
test-thread: fft-host
	mkdir -p $(H)
	$(HOST_CXX) -std=c++17 -O1 -g -pthread -fsanitize=thread -Isrc tests/test_queue.cpp -o $(H)/test_queue
	./$(H)/test_queue
	$(HOST_CXX) -std=c++17 -O1 -g -pthread -fsanitize=thread -Isrc tests/test_bridge.cpp $(DSP_SOURCES) -o $(H)/test_bridge
	./$(H)/test_bridge
	$(HOST_CXX) -std=c++17 -O1 -g -pthread -fsanitize=thread -Isrc -I$(BOOTLOADER_DIR) tests/test_sysex_thread.cpp \
		src/control.cpp -o $(H)/test_sysex_thread
	./$(H)/test_sysex_thread
	$(HOST_CXX) -std=c++17 -O1 -g -pthread -fsanitize=thread $(MOD_FLAGS) -Isrc tests/test_mod_thread.cpp -o $(H)/test_mod_thread_tsan
	./$(H)/test_mod_thread_tsan

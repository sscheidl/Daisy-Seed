# Daisy Program Manager: BOOT_QSPI application behind the unchanged Daisy bootloader.
# Build from this directory. Two profiles with separate build directories:
#   make PROFILE=probe    -> build/probe/DaisyProgramManagerProbe-v<version>.bin (LED/CDC/service, no SD start)
#   make                  -> build/manager/DaisyProgramManager-v<version>.bin   (complete version 1)
# Host tests (no ARM SDK needed): make test / make test-sanitize
# Image checks after an ARM build: make check   (never flashes anything)
# Flashing is a separate, human-requested step: see README.md (program-dfu to 0x90040000).
.DEFAULT_GOAL := all
DAISY_ROOT = ../..
PROFILE ?= manager
VERSION := $(shell sed -n 's/.*PROGRAM_MANAGER_VERSION "\([^"]*\)".*/\1/p' src/version.h)
ifeq ($(PROFILE),probe)
TARGET = DaisyProgramManagerProbe-v$(VERSION)
override BUILD_DIR = build/probe
C_DEFS += -DPM_PROFILE_PROBE=1
else ifeq ($(PROFILE),manager)
TARGET = DaisyProgramManager-v$(VERSION)
override BUILD_DIR = build/manager
C_DEFS += -DPM_PROFILE_MANAGER=1
else
$(error Unknown PROFILE '$(PROFILE)' (probe|manager))
endif
DEBUG = 0
OPT = -O2
# Execute in place from QSPI; the linker keeps 0x24000000..0x24078000 free (docs/CONTRACT.md).
APP_TYPE = BOOT_QSPI
LDSCRIPT = ld/program_manager_qspi.lds
CPP_STANDARD = -std=gnu++17

DIAG_DIR = $(DAISY_ROOT)/diagnostics/firmware/src
PROTOCOL_DIR = $(DAISY_ROOT)/diagnostics/protocol
BOOTLOADER_DIR = $(DAISY_ROOT)/common/bootloader
PM_SOURCES = src/pm_image.cpp src/pm_catalog.cpp src/pm_loader.cpp src/pm_manager.cpp src/pm_protocol.cpp
DIAG_SOURCES = $(DIAG_DIR)/diag_app.cpp $(DIAG_DIR)/diag_core.cpp $(PROTOCOL_DIR)/ddp.cpp
# libDaisy's Makefile keys objects by basename: every source needs a unique file name
# (hence pm_main.cpp, not main.cpp).
CPP_SOURCES = src/pm_main.cpp $(PM_SOURCES) $(DIAG_SOURCES)
C_SOURCES += src/pm_sha256.c
C_INCLUDES += -Isrc -I$(DIAG_DIR) -I$(PROTOCOL_DIR) -I$(BOOTLOADER_DIR) -I$(DAISY_ROOT)/common/sd/reader
# Source revision in PMINFO/HELLO (build=): short commit, "-dirty" with local changes.
PM_BUILD_REV := $(shell git rev-parse --short HEAD 2>/dev/null || echo nogit)$(shell git diff --quiet -- . 2>/dev/null || echo -dirty)
C_DEFS += -DPM_BUILD_REV=\"$(PM_BUILD_REV)\"
LIBDAISY_DIR = $(DAISY_ROOT)/libDaisy
C_INCLUDES += -I$(LIBDAISY_DIR)/Middlewares/Patched/ST/STM32_USB_Device_Library/Class/CDC/Inc
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
# Installed bootloader variant of the Desktop template; this project never installs one.
BOOT_BIN = $(SYSTEM_FILES_DIR)/dsy_bootloader_v6_4-extdfu-2000ms.bin

# Host tests must not need the ARM SDK or the submodules.
HOST_ONLY := $(if $(MAKECMDGOALS),$(if $(filter-out test test-sanitize test-python,$(MAKECMDGOALS)),,1),)
ifneq ($(HOST_ONLY),1)
ifeq ($(PROFILE),manager)
# Shared read-only SD driver (FatFs R0.16, exFAT); replaces libDaisy's FatFs R0.12c.
USE_FATFS = 0
include $(DAISY_ROOT)/common/sd/sd.mk
endif
include $(SYSTEM_FILES_DIR)/Makefile
$(BUILD_DIR): | build
build:
	mkdir -p build
$(BUILD_DIR)/$(TARGET).elf: $(LIBDAISY_DIR)/build/libdaisy.a
$(LIBDAISY_DIR)/build/libdaisy.a:
	"$(MAKE)" -C $(LIBDAISY_DIR)
endif

PYTHON ?= python3
# Image checks: QSPI placement, free SRAM target, vectors, marker, SD symbol policy.
.PHONY: check
check: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).map
	$(PYTHON) tools/check_qspi_image.py --profile $(PROFILE) --version $(VERSION) \
		$(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).map
ifeq ($(PROFILE),manager)
	"$(MAKE)" sd-check SD_CHECK_REQUIRE=disk_read,f_mount,f_open,f_read,f_readdir,SDMMC_CmdReadSingleBlock
endif

# ---- Host tests -------------------------------------------------------------------
HOST_CC ?= gcc
HOST_CXX ?= g++
HOST_FLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror
HOST_CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
HOST_INC = -Isrc -Itests -I$(DIAG_DIR) -I$(PROTOCOL_DIR) -I$(BOOTLOADER_DIR)
HOST_BUILD = build/host
HOST_TESTS = test_sha256 test_image test_catalog test_loader test_manager test_protocol test_diag_hooks test_return_entry
.PHONY: test test-sanitize test-python
test: $(addprefix run-,$(HOST_TESTS))

$(HOST_BUILD):
	mkdir -p $(HOST_BUILD)

$(HOST_BUILD)/pm_sha256.o: src/pm_sha256.c src/pm_sha256.h | $(HOST_BUILD)
	$(HOST_CC) $(HOST_CFLAGS) -Isrc -c $< -o $@

$(HOST_BUILD)/test_sha256: tests/test_sha256.cpp $(HOST_BUILD)/pm_sha256.o | $(HOST_BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INC) $< $(HOST_BUILD)/pm_sha256.o -o $@
$(HOST_BUILD)/test_image: tests/test_image.cpp src/pm_image.cpp | $(HOST_BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INC) $^ -o $@
$(HOST_BUILD)/test_catalog: tests/test_catalog.cpp src/pm_catalog.cpp src/pm_image.cpp | $(HOST_BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INC) $^ -o $@
$(HOST_BUILD)/test_loader: tests/test_loader.cpp src/pm_loader.cpp src/pm_image.cpp src/pm_catalog.cpp $(HOST_BUILD)/pm_sha256.o | $(HOST_BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INC) $^ -o $@
$(HOST_BUILD)/test_manager: tests/test_manager.cpp src/pm_manager.cpp src/pm_loader.cpp src/pm_image.cpp src/pm_catalog.cpp $(HOST_BUILD)/pm_sha256.o | $(HOST_BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INC) $^ -o $@
$(HOST_BUILD)/test_protocol: tests/test_protocol.cpp src/pm_protocol.cpp src/pm_manager.cpp src/pm_loader.cpp src/pm_image.cpp src/pm_catalog.cpp $(DIAG_SOURCES) $(HOST_BUILD)/pm_sha256.o | $(HOST_BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INC) $^ -o $@
$(HOST_BUILD)/test_diag_hooks: tests/test_diag_hooks.cpp $(DIAG_SOURCES) | $(HOST_BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INC) $^ -o $@
$(HOST_BUILD)/test_return_entry: tests/test_return_entry.cpp | $(HOST_BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INC) -Itests/fakes $< -o $@

run-%: $(HOST_BUILD)/%
	./$<

test-sanitize:
	"$(MAKE)" test HOST_BUILD=build/host-sanitize \
		HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" \
		HOST_CFLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# Python tools (packager, catalog/card checker, ELF checker, CDC client with fake transport).
test-python:
	$(PYTHON) -m unittest discover -s tests/python -p 'test_*.py' -v

# Shared Seed3 Desktop Dev Kit build settings; include from a project Makefile.
# Each project supplies TARGET and its own CPP_SOURCES and tests.
# Projects live two levels below the repository (instruments/<id>, effects/<id>);
# deeper projects (modules/mod-on-daisy/bench) set DAISY_ROOT before the include.
# Relative paths also avoid injecting spaces from a local Playground path into flags.
DAISY_ROOT ?= ../..
DEBUG = 0
OPT = -O3
APP_TYPE = BOOT_SRAM
CPP_STANDARD = -std=gnu++17
LIBDAISY_DIR = $(DAISY_ROOT)/libDaisy
DAISYSP_DIR = $(DAISY_ROOT)/DaisySP
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
BOOT_BIN = $(SYSTEM_FILES_DIR)/dsy_bootloader_v6_4-extdfu-2000ms.bin
# Shared software entry into that bootloader (keys S9+S16, SysEx); see its README.
BOOTLOADER_DIR = $(DAISY_ROOT)/common/bootloader
C_INCLUDES += -I$(BOOTLOADER_DIR)

include $(SYSTEM_FILES_DIR)/Makefile

# Variant directories (build/mod, build/ambient, build/fx-*, build/b256-max8): libDaisy
# creates $(BUILD_DIR) with a plain mkdir, which fails on a fresh checkout without build/.
ifneq ($(filter build/%,$(BUILD_DIR)),)
$(BUILD_DIR): | build
build:
	mkdir -p build
endif

.PHONY: libs
libs:
	"$(MAKE)" -C $(LIBDAISY_DIR)
	"$(MAKE)" -C $(DAISYSP_DIR)

$(BUILD_DIR)/$(TARGET).elf: $(LIBDAISY_DIR)/build/libdaisy.a $(DAISYSP_DIR)/build/libdaisysp.a
$(LIBDAISY_DIR)/build/libdaisy.a:
	"$(MAKE)" -C $(LIBDAISY_DIR)
$(DAISYSP_DIR)/build/libdaisysp.a:
	"$(MAKE)" -C $(DAISYSP_DIR)

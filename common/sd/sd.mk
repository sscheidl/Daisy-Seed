# Shared read-only SD driver (FatFs R0.16, exFAT). Include from a firmware Makefile
# BEFORE libDaisy's core/Makefile (or common/desktop.mk): its include paths must come
# ahead of libDaisy's FatFs R0.12c headers and ffconf.h. Set DAISY_ROOT first if the
# project is not two levels below the repository root.
DAISY_ROOT ?= ../..
SD_DIR := $(DAISY_ROOT)/common/sd

ifeq ($(USE_FATFS),1)
$(error common/sd replaces libDaisy FatFs: set USE_FATFS = 0)
endif
ifdef FATFS_DIR
$(error include common/sd/sd.mk before libDaisy core/Makefile)
endif

C_SOURCES += $(SD_DIR)/ro_sd.c $(SD_DIR)/fatfs/ff.c $(SD_DIR)/fatfs/ffunicode.c
C_INCLUDES += -I$(SD_DIR) -I$(SD_DIR)/fatfs

# ELF policy check: make sd-check after the firmware build.
SD_CHECK_NM ?= arm-none-eabi-nm
SD_CHECK_PYTHON ?= python3
SD_CHECK_REQUIRE ?= disk_read,f_mount,f_readdir,SDMMC_CmdReadSingleBlock
# Comma-separated names removed from the ban list, e.g. for a card-reader image.
SD_CHECK_ALLOW ?=
.PHONY: sd-check
sd-check: $(BUILD_DIR)/$(TARGET).elf
	$(SD_CHECK_PYTHON) $(DAISY_ROOT)/scripts/check-sd-symbols.py --nm $(SD_CHECK_NM) \
		--require $(SD_CHECK_REQUIRE) $(if $(SD_CHECK_ALLOW),--allow $(SD_CHECK_ALLOW)) $<

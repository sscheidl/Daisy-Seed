# Shared SD driver (FatFs R0.16, exFAT), read-only unless SD_WRITABLE = 1. Include from a firmware Makefile
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
# SD_WRITABLE = 1 selects rw/ffconf.h (FF_FS_READONLY 0) for firmware that saves files
# itself (Looper). Default: read-only, no write path in the image.
SD_WRITABLE ?= 0
ifeq ($(SD_WRITABLE),1)
C_INCLUDES += -I$(SD_DIR)/rw
endif
C_INCLUDES += -I$(SD_DIR) -I$(SD_DIR)/fatfs

# ELF policy check: make sd-check after the firmware build. It applies the policy of the
# image's profile (sd_policy.mk) and then injects each writer of SD_CHECK_PROBES into the
# same check, which must reject it.
include $(SD_DIR)/sd_policy.mk
SD_CHECK_NM ?= arm-none-eabi-nm
SD_CHECK_CC ?= arm-none-eabi-gcc
SD_CHECK_PYTHON ?= python3
ifeq ($(SD_WRITABLE),1)
SD_CHECK_ALLOW ?= $(SD_WRITE_ALLOW)
SD_CHECK_REQUIRE ?= disk_read,disk_write,f_mount,f_readdir,f_write,SDMMC_CmdReadSingleBlock
SD_CHECK_PROBES ?= $(SD_WRITE_PROBES)
endif
# Comma-separated names removed from the ban list, e.g. for a card-reader image.
SD_CHECK_ALLOW ?=
SD_CHECK_REQUIRE ?= disk_read,f_mount,f_readdir,SDMMC_CmdReadSingleBlock
SD_CHECK_PROBES ?= $(SD_READONLY_PROBES)
.PHONY: sd-check
sd-check: $(BUILD_DIR)/$(TARGET).elf
	$(SD_CHECK_PYTHON) $(DAISY_ROOT)/scripts/check-sd-symbols.py --nm $(SD_CHECK_NM) \
		--require $(SD_CHECK_REQUIRE) $(if $(SD_CHECK_ALLOW),--allow $(SD_CHECK_ALLOW)) \
		--probe $(SD_CHECK_PROBES) --probe-cc $(SD_CHECK_CC) $<

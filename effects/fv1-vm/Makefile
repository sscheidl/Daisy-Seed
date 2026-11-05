# FV-1 interpreter FX component and its standalone firmware (research / feasibility study).
#   make PROJECT=fv1-vm-fx            ARM image with the two own demo programs (float, SDRAM)
#   PRECISION=double  MEMORY=axi      measurement variants, each with its own build directory
#   PROGRAMS=local                    program table from generated/fv1vm_programs.local.h
#                                     (tools/gen_programs.py); such images are LOCAL ONLY
.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*FV1VM_FIRMWARE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
PRECISION ?= float
MEMORY ?= sdram
PROGRAMS ?= builtin
$(if $(filter $(PRECISION),float double),,$(error PRECISION must be float or double))
$(if $(filter $(MEMORY),sdram axi),,$(error MEMORY must be sdram or axi))
$(if $(filter $(PROGRAMS),builtin local),,$(error PROGRAMS must be builtin or local))
LOCAL_TABLE = generated/fv1vm_programs.local.h
ifeq ($(PROGRAMS),local)
$(if $(wildcard $(LOCAL_TABLE)),,$(error $(LOCAL_TABLE) is missing; run tools/gen_programs.py first))
TARGET = Fv1VmFxLocal-v$(VERSION)
C_INCLUDES += -Igenerated
else
TARGET = Fv1VmFx-v$(VERSION)
endif
ifneq ($(PRECISION)-$(MEMORY)-$(PROGRAMS),float-sdram-builtin)
override BUILD_DIR = build/$(PROGRAMS)-$(PRECISION)-$(MEMORY)
endif
C_DEFS = -DFV1VM_DOUBLE=$(if $(filter double,$(PRECISION)),1,0) \
         -DFV1VM_FAST_MEMORY=$(if $(filter axi,$(MEMORY)),1,0) \
         -DFV1VM_LOCAL_PROGRAMS=$(if $(filter local,$(PROGRAMS)),1,0)
C_INCLUDES += -Iinclude -Isrc
CPP_SOURCES = src/main.cpp
include ../../common/desktop.mk

ifeq ($(MEMORY),axi)
# libDaisy's pinned BOOT_SRAM script plus one NOLOAD section behind the image (the delay RAM).
LDSCRIPT := $(BUILD_DIR)/fv1vm_axi.lds
$(BUILD_DIR)/$(TARGET).elf: $(LDSCRIPT)
$(LDSCRIPT): $(LIBDAISY_DIR)/core/STM32H750IB_sram.lds Makefile | $(BUILD_DIR)
	awk '/^[ \t]*DISCARD[ \t]*:/ && !done { \
	  print "    .fv1_axi_bss ALIGN(LOADADDR(.data) + SIZEOF(.data), 32) (NOLOAD) :"; \
	  print "    {"; print "        __fv1_axi_start = .;"; print "        KEEP(*(.fv1_axi_bss*))"; \
	  print "        . = ALIGN(32);"; print "        __fv1_axi_end = .;"; print "    } > SRAM"; \
	  print "    ASSERT(__fv1_axi_start >= LOADADDR(.data) + SIZEOF(.data), \"delay RAM overlaps image\")"; \
	  print "    ASSERT(__fv1_axi_end + 8192 <= ORIGIN(SRAM) + LENGTH(SRAM), \"insufficient AXI reserve\")"; \
	  done = 1 } { print }' $< > $@
endif

.PHONY: test test-sanitize test-thread tool version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror
HOST_INCLUDES = -Iinclude -Isrc
PYTHON ?= python3

test:
	mkdir -p build/host
	"$(HOST_CXX)" $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_fv1_vm.cpp -o build/host/test_fv1_vm
	./build/host/test_fv1_vm
	"$(HOST_CXX)" $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_firmware.cpp -o build/host/test_firmware
	./build/host/test_firmware
	"$(HOST_CXX)" $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_fx48.cpp -o build/host/test_fx48
	./build/host/test_fx48
	"$(PYTHON)" -m unittest discover -s tests -p test_probe.py

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# No threads of its own: controls and DSP run in the audio callback.
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

# Host runner for cross-checks with an external reference; program files are never part of this repository.
tool:
	mkdir -p build/host
	"$(HOST_CXX)" $(HOST_FLAGS) $(HOST_INCLUDES) tools/fv1vm_run.cpp -o build/host/fv1vm_run

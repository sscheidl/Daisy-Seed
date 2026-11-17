.DEFAULT_GOAL := all
# S1 load spike of the Beads port study (README.md). Images from one source:
#   make              MEM=A: BOOT_SRAM, buffer in SDRAM            -> build/a/TextureSpikeA-v<version>.bin
#   make ITCM=1       as A, hot DSP code in ITCM                   -> build/a-itcm/TextureSpikeAI-v<version>.bin
#   make MEM=B        BOOT_QSPI, code in QSPI, buffer in AXI + D2  -> build/b/TextureSpikeB-v<version>.bin
#   make MEM=B ITCM=1 as B, hot DSP code in ITCM, hot tables in DTCM -> build/b-itcm/TextureSpikeBI-v<version>.bin
# Block size (12/48) and product load are chosen at run time (CC116).
# Not in projects.mk: a measurement firmware for one decision (D8), not a product.
VERSION := $(shell sed -n 's/.*TEXTURE_SPIKE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
MEM ?= A
ITCM ?= 0
MOD_ON_DAISY = ../../../modules/mod-on-daisy
EURORACK = ../third_party/eurorack
DSP_SOURCES = $(EURORACK)/beads/dsp/processor.cc $(EURORACK)/beads/dsp/synth.cc $(EURORACK)/beads/dsp/mu_law.cc \
	$(EURORACK)/beads/dsp/delay_clock.cc $(EURORACK)/beads/dsp/delay_controller.cc \
	$(EURORACK)/beads/dsp/trigger_generator.cc $(EURORACK)/beads/resources.cc \
	$(EURORACK)/stmlib/dsp/units.cc $(EURORACK)/stmlib/utils/random.cc
ifeq ($(MEM)$(ITCM),A0)
TARGET = TextureSpikeA-v$(VERSION)
override BUILD_DIR = build/a
else ifeq ($(MEM)$(ITCM),A1)
TARGET = TextureSpikeAI-v$(VERSION)
override BUILD_DIR = build/a-itcm
LDSCRIPT = src/memory_a_itcm.lds
C_DEFS += -DTEXTURE_SPIKE_ITCM
else ifeq ($(MEM)$(ITCM),B0)
TARGET = TextureSpikeB-v$(VERSION)
override BUILD_DIR = build/b
# desktop.mk assigns BOOT_SRAM; override keeps this value.
override APP_TYPE = BOOT_QSPI
LDSCRIPT = src/memory_b.lds
C_DEFS += -DTEXTURE_SPIKE_MEMORY_B
else ifeq ($(MEM)$(ITCM),B1)
TARGET = TextureSpikeBI-v$(VERSION)
override BUILD_DIR = build/b-itcm
override APP_TYPE = BOOT_QSPI
LDSCRIPT = src/memory_b_itcm.lds
C_DEFS += -DTEXTURE_SPIKE_MEMORY_B -DTEXTURE_SPIKE_ITCM
else
$(error MEM must be A or B, ITCM 0 or 1)
endif
CPP_SOURCES = src/main.cpp
C_DEFS += -DTEXTURE_NO_HARDWARE_24K
C_INCLUDES = -Isrc -I$(EURORACK) -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
DAISY_ROOT := ../../..
include ../../../common/desktop.mk
ifeq ($(MEM)$(ITCM),A0)
LDFLAGS += -Tsrc/memory_a.ld
$(BUILD_DIR)/$(TARGET).elf: src/memory_a.ld
else
$(BUILD_DIR)/$(TARGET).elf: $(LDSCRIPT)
endif
CPPFLAGS += -fstack-usage

# The upstream .cc files, compiled with the firmware's flags (as in instruments/physical-modeling).
CC_SOURCES = $(DSP_SOURCES)
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
$(BUILD_DIR)/$(TARGET).elf: $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
vpath %.cc $(sort $(dir $(CC_SOURCES)))
$(BUILD_DIR)/%.o: %.cc Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) $< -o $@

.PHONY: test test-sanitize version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror
# -include cstdio: the upstream TEST trace calls printf without including it (wavetable_granular_player.h).
HOST_DEFS = -DTEST -DTEXTURE_NO_HARDWARE_24K -D_USE_MATH_DEFINES -include cstdio
POINT_BLOCKS ?= 4000
# Upstream code is built with the same flags and its warnings silenced; ours stay errors.
# stdout is the upstream TEST trace (voice stealing in synth mode); the test reports on stderr.
test:
	mkdir -p build/host
	cd build/host && $(HOST_CXX) $(HOST_FLAGS) -w $(HOST_DEFS) -I../../$(EURORACK) -c $(addprefix ../../,$(DSP_SOURCES))
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_DEFS) -Isrc -isystem $(EURORACK) -I$(MOD_ON_DAISY)/include tests/test_spike.cpp build/host/*.o -o build/host/test_spike
	./build/host/test_spike $(POINT_BLOCKS) > build/host/upstream_trace.txt

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"

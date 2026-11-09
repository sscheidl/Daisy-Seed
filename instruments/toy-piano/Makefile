.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*TOY_PIANO_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
AMBIENT ?= 0
ifneq ($(filter-out undefined,$(origin AMB_GATE) $(origin AMB_WET32)),)
$(error AMB_GATE/AMB_WET32 are obsolete; shared Ambient 0.4 always uses gating and 32-12 kHz)
endif
TAPE ?= 0
FV1 ?= 0
FILTER ?= 0
# Toy Piano on Cumulonimbus: CUMULO=1, CUMULO_RATE=48|32 (internal grain rate), CUMULO_PROFILE=0|1.
CUMULO ?= 0
CUMULO_RATE ?= 48
CUMULO_PROFILE ?= 0
ifeq ($(FV1),1)
$(if $(filter 1,$(TAPE) $(AMBIENT)),$(error FV1=1 cannot be combined with TAPE=1 or AMBIENT=1))
endif
ifeq ($(CUMULO),1)
$(if $(filter 1,$(TAPE) $(AMBIENT) $(FV1) $(FILTER)),$(error CUMULO=1 cannot be combined with another FX build))
$(if $(filter $(CUMULO_RATE),48 32),,$(error CUMULO_RATE must be 48 or 32))
$(if $(filter $(CUMULO_PROFILE),0 1),,$(error CUMULO_PROFILE must be 0 or 1))
VERSION := $(shell sed -n 's/.*TOY_PIANO_CUMULONIMBUS_VERSION "\(.*\)".*/\1/p' src/composed_version.h | head -n 1)
CUMULO_SUFFIX := $(if $(filter 1,$(CUMULO_PROFILE)),Profile,)
TARGET = ToyPianoCumulonimbus$(CUMULO_RATE)$(CUMULO_SUFFIX)-v$(VERSION)
override BUILD_DIR = build/cumulonimbus$(CUMULO_RATE)$(if $(filter 1,$(CUMULO_PROFILE)),-profile,)
CUMULO_INCLUDES = -I../../instruments/physical-modeling/torus -I../../instruments/physical-modeling/third_party
FX_DEFS = -DTOY_PIANO_CUMULONIMBUS=1 -DTOY_PIANO_CUMULONIMBUS_PROFILE=$(CUMULO_PROFILE) \
          -DCUMULONIMBUS_INTERNAL_RATE=$(CUMULO_RATE)000 -DMOD_ON_DAISY_EXTENSIONS=1
C_DEFS = $(FX_DEFS)
C_INCLUDES = $(CUMULO_INCLUDES)
else ifeq ($(FILTER),1)
$(if $(filter 1,$(TAPE) $(AMBIENT) $(FV1)),$(error FILTER=1 stands alone: the test build has no effects))
VERSION := $(shell sed -n 's/.*TOY_PIANO_FILTER_VERSION "\(.*\)".*/\1/p' src/composed_version.h | head -n 1)
TARGET = ToyPianoFilter-v$(VERSION)
override BUILD_DIR = build/filter
FX_DEFS = -DTOY_PIANO_FILTER=1 -DMOD_ON_DAISY_EXTENSIONS=1
C_DEFS = $(FX_DEFS)
else ifeq ($(TAPE),1)
VERSION := $(shell sed -n 's/.*TOY_PIANO_TAPE_VERSION "\(.*\)".*/\1/p' src/composed_version.h | head -n 1)
TARGET = ToyPianoTape-v$(VERSION)
override BUILD_DIR = build/tape
FX_DEFS = -DTOY_PIANO_TAPE=1 -DMOD_ON_DAISY_EXTENSIONS=1
C_DEFS = $(FX_DEFS)
else ifeq ($(FV1),1)
# Research / feasibility study: Toy Piano -> fv1-vm. PROGRAMS=local uses the git-ignored table of
# effects/fv1-vm/generated (tools/gen_programs.py); such images are LOCAL ONLY, never released.
FV1_DIR = ../../effects/fv1-vm
FV1_PROGRAMS ?= builtin
$(if $(filter $(FV1_PROGRAMS),builtin local),,$(error FV1_PROGRAMS must be builtin or local))
VERSION := $(shell sed -n 's/.*TOY_PIANO_FV1_VERSION "\(.*\)".*/\1/p' src/composed_version.h | head -n 1)
ifeq ($(FV1_PROGRAMS),local)
$(if $(wildcard $(FV1_DIR)/generated/fv1vm_programs.local.h),,$(error $(FV1_DIR)/generated/fv1vm_programs.local.h is missing))
TARGET = ToyPianoFv1Local-v$(VERSION)
override BUILD_DIR = build/fv1-local
FX_DEFS = -DTOY_PIANO_FV1=1 -DMOD_ON_DAISY_EXTENSIONS=1 -DFV1VM_LOCAL_PROGRAMS=1
FV1_INCLUDES = -I$(FV1_DIR)/include -I$(FV1_DIR)/src -I$(FV1_DIR)/generated
C_INCLUDES = $(FV1_INCLUDES)
else
TARGET = ToyPianoFv1-v$(VERSION)
override BUILD_DIR = build/fv1
FX_DEFS = -DTOY_PIANO_FV1=1 -DMOD_ON_DAISY_EXTENSIONS=1 -DFV1VM_LOCAL_PROGRAMS=0
FV1_INCLUDES = -I$(FV1_DIR)/include -I$(FV1_DIR)/src
C_INCLUDES = $(FV1_INCLUDES)
endif
C_DEFS = $(FX_DEFS)
else ifeq ($(AMBIENT),1)
VERSION := $(shell sed -n 's/.*TOY_PIANO_AMBIENT_VERSION "\(.*\)".*/\1/p' src/composed_version.h | head -n 1)
TARGET = ToyPianoAmbient-v$(VERSION)
# Diagnostic reverb memory experiments, all off by default (bit-identical audio):
# AMB_WA=1 SDRAM write-allocate, AMB_COMPACT=1 48 kHz buffers with all-pass/pitch
# lines in AXI SRAM, AMB_FIR=1 contiguous anti-alias FIR. Own build directory.
AMB_WA ?= 0
AMB_COMPACT ?= 0
AMB_FIR ?= 0
ifeq ($(AMB_WA)$(AMB_COMPACT)$(AMB_FIR),000)
override BUILD_DIR = build/ambient
else
override BUILD_DIR = build/ambient-exp-w$(AMB_WA)-c$(AMB_COMPACT)-f$(AMB_FIR)
endif
FX_DEFS = -DTOY_PIANO_FX=1 -DMOD_ON_DAISY_EXTENSIONS=1 -DAMBIENT_SDRAM_WRITE_ALLOCATE=$(AMB_WA) \
          -DAMBIENT_COMPACT_48K=$(AMB_COMPACT) -DAMBIENT_CONTIGUOUS_FIR=$(AMB_FIR)
C_DEFS = $(FX_DEFS)
FX_SOURCES = ../../effects/reverb/src/ambient_reverb.cpp ../../effects/reverb/src/wet_path.cpp
C_INCLUDES = -I../../sequencers/step-seq/include
else
TARGET = ToyPiano-v$(VERSION)
# v0.4: all Mod on Daisy extensions as in Physical Modeling MOD (extended monitor with
# pot positions, LFO cross modulation) and with them the echo guard (PROTOCOL.md § 2, 2a).
MOD_DEFS = -DMOD_ON_DAISY_EXTENSIONS=1
C_DEFS = $(MOD_DEFS)
endif
CPP_SOURCES = src/main.cpp src/toy_piano.cpp $(FX_SOURCES)
MOD_ON_DAISY = ../../modules/mod-on-daisy
C_INCLUDES += -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
include ../../common/desktop.mk
ifeq ($(AMBIENT)-$(AMB_COMPACT),1-1)
LDSCRIPT := ld/toy_piano_axi.lds
$(BUILD_DIR)/$(TARGET).elf: $(LDSCRIPT)
endif
ifeq ($(FV1),1)
LDSCRIPT := ld/toy_piano_axi.lds
$(BUILD_DIR)/$(TARGET).elf: $(LDSCRIPT)
endif
ifeq ($(FILTER),1)
LDSCRIPT := ld/toy_piano_axi.lds
$(BUILD_DIR)/$(TARGET).elf: $(LDSCRIPT)
endif
ifeq ($(CUMULO),1)
LDSCRIPT := ld/toy_piano_axi.lds
$(BUILD_DIR)/$(TARGET).elf: $(LDSCRIPT)
endif
ifeq ($(AMBIENT),1)
# Prevent the official flattened VPATH from finding the standalone FX's main.cpp.
$(BUILD_DIR)/main.o: src/main.cpp Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) $< -o $@
endif

.PHONY: test test-sanitize test-thread version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
HOST_INCLUDES = $(MOD_DEFS) -Isrc -I$(BOOTLOADER_DIR) -I$(DAISYSP_DIR)/Source -I$(LIBDAISY_DIR)/src -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
ifeq ($(AMBIENT),1)
HOST_INCLUDES += -I../../sequencers/step-seq/include
endif
ifeq ($(CUMULO),1)
# Both internal rates and both image kinds share these host tests; the golden dry check runs on each.
CUMULO_HOST = -DTEST -isystem ../../instruments/physical-modeling/torus -isystem ../../instruments/physical-modeling/third_party -D_USE_MATH_DEFINES
test:
	mkdir -p build/host/cumulonimbus
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) $(CUMULO_HOST) -DTOY_PIANO_CUMULONIMBUS=1 -DTOY_PIANO_CUMULONIMBUS_PROFILE=0 -DMOD_ON_DAISY_EXTENSIONS=1 -DCUMULONIMBUS_INTERNAL_RATE=48000 tests/test_cumulonimbus.cpp src/toy_piano.cpp -o build/host/cumulonimbus/test48
	./build/host/cumulonimbus/test48
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) $(CUMULO_HOST) -DTOY_PIANO_CUMULONIMBUS=1 -DTOY_PIANO_CUMULONIMBUS_PROFILE=0 -DMOD_ON_DAISY_EXTENSIONS=1 -DCUMULONIMBUS_INTERNAL_RATE=32000 tests/test_cumulonimbus.cpp src/toy_piano.cpp -o build/host/cumulonimbus/test32
	./build/host/cumulonimbus/test32
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) $(CUMULO_HOST) -DTOY_PIANO_CUMULONIMBUS=1 -DTOY_PIANO_CUMULONIMBUS_PROFILE=1 -DMOD_ON_DAISY_EXTENSIONS=1 -DCUMULONIMBUS_INTERNAL_RATE=48000 tests/test_cumulonimbus.cpp src/toy_piano.cpp -o build/host/cumulonimbus/test48_profile
	./build/host/cumulonimbus/test48_profile
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) $(CUMULO_HOST) -DTOY_PIANO_CUMULONIMBUS=1 -DTOY_PIANO_CUMULONIMBUS_PROFILE=1 -DMOD_ON_DAISY_EXTENSIONS=1 -DCUMULONIMBUS_INTERNAL_RATE=32000 tests/test_cumulonimbus.cpp src/toy_piano.cpp -o build/host/cumulonimbus/test32_profile
	./build/host/cumulonimbus/test32_profile
else ifeq ($(FILTER),1)
test:
	mkdir -p build/host/filter
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) $(FX_DEFS) tests/test_filter.cpp src/toy_piano.cpp -o build/host/filter/test_filter
	./build/host/filter/test_filter
else ifeq ($(FV1),1)
HOST_INCLUDES += $(FV1_INCLUDES)
test:
	mkdir -p build/host/fv1
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) $(FX_DEFS) tests/test_fv1.cpp src/toy_piano.cpp -o build/host/fv1/test_fv1
	./build/host/fv1/test_fv1
else ifeq ($(TAPE),1)
test:
	mkdir -p build/host/tape
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) $(FX_DEFS) tests/test_tape.cpp src/toy_piano.cpp -o build/host/tape/test_tape
	./build/host/tape/test_tape
else ifeq ($(AMBIENT),1)
test:
	mkdir -p build/host/ambient
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) $(FX_DEFS) tests/test_ambient.cpp src/toy_piano.cpp $(FX_SOURCES) -o build/host/ambient/test_ambient
	./build/host/ambient/test_ambient
	# Diagnostic memory switches must render bit-identically to the default build.
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) -I../../effects/reverb/src tests/test_ambient_experiments.cpp src/toy_piano.cpp $(FX_SOURCES) -o build/host/ambient/experiments_reference
	./build/host/ambient/experiments_reference write build/host/ambient/experiments.f32
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) -I../../effects/reverb/src -DAMBIENT_COMPACT_48K=1 -DAMBIENT_CONTIGUOUS_FIR=1 tests/test_ambient_experiments.cpp src/toy_piano.cpp $(FX_SOURCES) -o build/host/ambient/experiments_variant
	./build/host/ambient/experiments_variant compare build/host/ambient/experiments.f32
else
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_toy_piano.cpp src/toy_piano.cpp -o build/host/test_toy_piano
	./build/host/test_toy_piano
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) tests/test_callback.cpp src/toy_piano.cpp -o build/host/test_callback
	./build/host/test_callback
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) tests/test_golden.cpp src/toy_piano.cpp -o build/host/test_golden
	./build/host/test_golden
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) tests/test_modulation.cpp src/toy_piano.cpp -o build/host/test_modulation
	./build/host/test_modulation
endif

test-sanitize:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

test-thread:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

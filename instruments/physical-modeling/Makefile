.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*PHYSICAL_MODELING_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
# MOD=1: the separate Mod on Daisy build (docs/MODULATION.md), own image and build
# directory; the default build contains no Mod on Daisy integration.
MOD ?= 0
PM_OPTIMIZE ?= 1
PM_MEMORY ?= sdram
PM_LOOP_INVARIANTS ?= 0
PM_PROFILE ?= 0
PM_RENDER_HASH ?= 0
COMPOSED ?= 0
FX ?= torus
MOD_ON_DAISY = ../../modules/mod-on-daisy
MOD_DEFS = -DPHYSICAL_MODELING_MOD=1 -DTORUS_VOICE_MODULATION=1 -DMOD_ON_DAISY_EXTENSIONS=1
MOD_FLAGS = $(MOD_DEFS) -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
ifeq ($(COMPOSED),1)
ifneq ($(MOD),1)
$(error COMPOSED=1 requires MOD=1 in this first pilot)
endif
ifneq ($(filter $(FX),torus ambient),$(FX))
$(error FX must be torus or ambient)
endif
ifeq ($(strip $(FX)),)
$(error FX must not be empty)
endif
VERSION := $(shell sed -n 's/.*PHYSICAL_MODELING_COMPOSED_VERSION "\(.*\)".*/\1/p' src/composed_version.h | head -n 1)
MOD_DEFS += -DPHYSICAL_MODELING_COMPOSED=1
ifeq ($(FX),ambient)
MOD_DEFS += -DPHYSICAL_MODELING_AMBIENT=1
COMPOSED_IMAGE = PhysicalModelingFxAmbient
FX_SOURCES = ../../effects/reverb/src/ambient_reverb.cpp ../../effects/reverb/src/wet_path.cpp
else
COMPOSED_IMAGE = PhysicalModelingFxTorus
endif
endif
ifeq ($(MOD),1)
ifeq ($(COMPOSED),1)
TARGET = $(COMPOSED_IMAGE)-v$(VERSION)
override BUILD_DIR = build/fx-$(FX)
else
TARGET = PhysicalModelingMod-v$(VERSION)
override BUILD_DIR = build/mod
endif
C_DEFS = $(MOD_DEFS)
C_INCLUDES += -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
else
TARGET = PhysicalModeling-v$(VERSION)
endif
DSP_SOURCES = torus/dsp/part.cpp torus/dsp/fm_voice.cpp torus/dsp/string.cpp torus/dsp/resonator.cpp torus/resources.cpp third_party/stmlib/dsp/units.cc third_party/stmlib/utils/random.cc
CPP_SOURCES = src/main.cpp src/physical_modeling.cpp src/midi_parser.cpp $(filter %.cpp,$(DSP_SOURCES)) $(FX_SOURCES)
C_INCLUDES += -Itorus -Ithird_party -Isrc -Wno-unused-local-typedefs -fstack-usage
# Select paths before the SDK expands its targets/OBJECTS, not after inclusion.
ifneq ($(PM_OPTIMIZE)-$(PM_MEMORY)-$(PM_LOOP_INVARIANTS)-$(PM_PROFILE)-$(PM_RENDER_HASH),1-sdram-0-0-0)
override BUILD_DIR := $(if $(BUILD_DIR),$(BUILD_DIR),build)/perf-o$(PM_OPTIMIZE)-$(PM_MEMORY)-i$(PM_LOOP_INVARIANTS)-p$(PM_PROFILE)-h$(PM_RENDER_HASH)
endif
include ../../common/desktop.mk
ifeq ($(PM_MEMORY),axi)
ifeq ($(COMPOSED)-$(FX),1-ambient)
$(error Ambient storage exceeds AXI capacity; use PM_MEMORY=sdram)
endif
LDSCRIPT := ld/physical_modeling_axi.lds
CPPFLAGS += -DPM_MEMORY_AXI=1
else ifneq ($(PM_MEMORY),sdram)
$(error PM_MEMORY must be sdram or axi)
endif
ifeq ($(PM_RENDER_HASH),1)
ifneq ($(PM_PROFILE),1)
$(error PM_RENDER_HASH=1 requires PM_PROFILE=1)
endif
endif
CPPFLAGS += -DTORUS_OPTIMIZE=$(PM_OPTIMIZE) -DPM_PROFILE=$(PM_PROFILE) -DPM_RENDER_HASH=$(PM_RENDER_HASH)
ifeq ($(PM_LOOP_INVARIANTS),1)
# Last option wins. Keep flags recursive: CFLAGS contains $@ for dependency files.
CPPFLAGS += -fmove-loop-invariants
endif
ifeq ($(COMPOSED),1)
# The official makefile flattens object names and sorts source VPATHs. Ambient's
# directory also contains a main.cpp; bind this product entry point explicitly.
$(BUILD_DIR)/main.o: src/main.cpp Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) $< -o $@
endif
PM_BUILD_PARENT := $(patsubst %/,%,$(dir $(BUILD_DIR)))
$(BUILD_DIR): | $(PM_BUILD_PARENT)
ifneq ($(PM_BUILD_PARENT),build)
$(PM_BUILD_PARENT):
	mkdir -p $@
endif
CC_SOURCES = $(filter %.cc,$(DSP_SOURCES))
$(BUILD_DIR)/$(TARGET).elf: $(LDSCRIPT)
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
$(BUILD_DIR)/$(TARGET).elf: $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
vpath %.cc $(sort $(dir $(CC_SOURCES)))
$(BUILD_DIR)/%.o: %.cc Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) $< -o $@

HOST_CXX ?= g++
HOST_PYTHON ?= python3
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
HOST_INCLUDES = -DTEST -D_USE_MATH_DEFINES -Isrc -I$(BOOTLOADER_DIR) -Itorus -Ithird_party -Wno-unused-local-typedefs -Wno-unused-parameter
.PHONY: test test-sanitize test-thread version
version:
	@echo $(VERSION)

test: test-performance

.PHONY: test-performance test-performance-one test-performance-mod test-profile
PERFORMANCE_FLAGS = $(if $(filter 1,$(MOD)),$(MOD_FLAGS),)
PERFORMANCE_DIR = build/host/performance$(if $(filter 1,$(MOD)),-mod,)
test-performance: test-performance-one test-profile
ifeq ($(MOD),0)
test-performance: test-performance-mod
endif
test-performance-mod: PERFORMANCE_FLAGS = $(MOD_FLAGS)
test-performance-mod: PERFORMANCE_DIR = build/host/performance-mod
test-profile:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) -DPM_PROFILE=1 -Itests/fakes -I$(LIBDAISY_DIR)/src tests/test_profile.cpp src/physical_modeling.cpp $(DSP_SOURCES) -o build/host/test_profile
	./build/host/test_profile
	$(HOST_PYTHON) -m unittest discover -s tests -p test_performance_probe.py -v
test-performance-one test-performance-mod:
	mkdir -p $(PERFORMANCE_DIR)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) $(PERFORMANCE_FLAGS) -DTORUS_OPTIMIZE=0 tests/test_performance.cpp src/physical_modeling.cpp $(DSP_SOURCES) $(FX_SOURCES) -o $(PERFORMANCE_DIR)/reference
	./$(PERFORMANCE_DIR)/reference write $(PERFORMANCE_DIR)/reference.f32
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) $(PERFORMANCE_FLAGS) -DTORUS_OPTIMIZE=1 tests/test_performance.cpp src/physical_modeling.cpp $(DSP_SOURCES) $(FX_SOURCES) -o $(PERFORMANCE_DIR)/optimized
	./$(PERFORMANCE_DIR)/optimized compare $(PERFORMANCE_DIR)/reference.f32
ifeq ($(COMPOSED),1)
test:
	mkdir -p build/host/fx-$(FX)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) $(MOD_FLAGS) -Itests/fakes -I$(LIBDAISY_DIR)/src tests/test_composed.cpp src/physical_modeling.cpp $(DSP_SOURCES) $(FX_SOURCES) -o build/host/fx-$(FX)/test_composed
	./build/host/fx-$(FX)/test_composed
ifeq ($(FX),torus)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) $(MOD_FLAGS) -Itests/fakes -I$(LIBDAISY_DIR)/src tests/test_golden.cpp src/physical_modeling.cpp $(DSP_SOURCES) -o build/host/fx-$(FX)/test_golden
	./build/host/fx-$(FX)/test_golden
endif
else
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_synth.cpp src/physical_modeling.cpp $(DSP_SOURCES) -o build/host/test_synth
	./build/host/test_synth
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) -Itests/fakes -I../../libDaisy/src tests/test_callback.cpp src/physical_modeling.cpp $(DSP_SOURCES) -o build/host/test_callback
	./build/host/test_callback
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) -I../../libDaisy/src tests/test_parser.cpp src/midi_parser.cpp -o build/host/test_parser
	./build/host/test_parser
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_review.cpp src/physical_modeling.cpp $(DSP_SOURCES) -o build/host/test_review
	./build/host/test_review
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) -Itests/fakes -I../../libDaisy/src tests/test_golden.cpp src/physical_modeling.cpp $(DSP_SOURCES) -o build/host/test_golden
	./build/host/test_golden
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) $(MOD_FLAGS) -Itests/fakes -I../../libDaisy/src tests/test_golden.cpp src/physical_modeling.cpp $(DSP_SOURCES) -o build/host/test_golden_mod
	./build/host/test_golden_mod
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) $(MOD_FLAGS) -Itests/fakes -I../../libDaisy/src tests/test_modulation.cpp src/physical_modeling.cpp $(DSP_SOURCES) -o build/host/test_modulation
	./build/host/test_modulation
	$(HOST_PYTHON) -m unittest discover -s tests -p test_cpu_probe.py -v
endif
test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -pthread -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer"
test-thread:
	mkdir -p build/host
	$(HOST_CXX) -std=c++17 -O1 -g -pthread -fsanitize=thread $(HOST_INCLUDES) -Itests/fakes -I../../libDaisy/src tests/test_queue.cpp -o build/host/test_queue
	./build/host/test_queue

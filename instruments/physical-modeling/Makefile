.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*PHYSICAL_MODELING_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
# MOD=1: the separate Mod on Daisy build (docs/MODULATION.md), own image and build
# directory; the default build compiles exactly the v0.1.0 sources.
MOD ?= 0
MOD_ON_DAISY = ../../modules/mod-on-daisy
MOD_DEFS = -DPHYSICAL_MODELING_MOD=1 -DTORUS_VOICE_MODULATION=1 -DMOD_ON_DAISY_EXTENSIONS=1
MOD_FLAGS = $(MOD_DEFS) -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
ifeq ($(MOD),1)
TARGET = PhysicalModelingMod-v$(VERSION)
override BUILD_DIR = build/mod
C_DEFS = $(MOD_DEFS)
C_INCLUDES += -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
else
TARGET = PhysicalModeling-v$(VERSION)
endif
DSP_SOURCES = torus/dsp/part.cpp torus/dsp/fm_voice.cpp torus/dsp/string.cpp torus/dsp/resonator.cpp torus/resources.cpp third_party/stmlib/dsp/units.cc third_party/stmlib/utils/random.cc
CPP_SOURCES = src/main.cpp src/physical_modeling.cpp src/midi_parser.cpp $(filter %.cpp,$(DSP_SOURCES))
C_INCLUDES += -Itorus -Ithird_party -Isrc -Wno-unused-local-typedefs -fstack-usage
include ../../common/desktop.mk
CC_SOURCES = $(filter %.cc,$(DSP_SOURCES))
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
test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -pthread -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer"
test-thread:
	mkdir -p build/host
	$(HOST_CXX) -std=c++17 -O1 -g -pthread -fsanitize=thread $(HOST_INCLUDES) -Itests/fakes -I../../libDaisy/src tests/test_queue.cpp -o build/host/test_queue
	./build/host/test_queue

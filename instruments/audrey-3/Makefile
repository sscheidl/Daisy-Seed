.DEFAULT_GOAL := all
# Component version (engine and controls) and standalone firmware version are separate.
VERSION := $(shell sed -n 's/.*AUDREY_3_FIRMWARE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = Audrey3-v$(VERSION)
AUDREY_II = third_party/audrey-ii
AUDREY_DSP = ../../common/dsp/audrey-ii
ECHO_DELAY = ../../effects/echo-delay
SC_REVERB = ../../effects/sc-reverb
STEP_SEQ = ../../sequencers/step-seq
# ReverbSc and Tone: the only LGPL code in this firmware (third_party/README.md). Only this
# project compiles them. The module is pinned by DaisySP; it is not part of libdaisysp.a here.
LGPL_DIR = ../../DaisySP/DaisySP-LGPL
ifneq ($(MAKECMDGOALS),version)
$(if $(wildcard $(LGPL_DIR)/Source/Effects/reverbsc.cpp),,$(error DaisySP-LGPL is missing: git -C DaisySP submodule update --init DaisySP-LGPL))
endif
LGPL_SOURCES = $(LGPL_DIR)/Source/Effects/reverbsc.cpp $(LGPL_DIR)/Source/Filters/tone.cpp
CPP_SOURCES = src/main.cpp $(AUDREY_DSP)/BiquadFilters.cpp $(LGPL_SOURCES)
C_INCLUDES = -Iinclude -I$(AUDREY_II) -I$(AUDREY_DSP) -I$(ECHO_DELAY)/include -I$(ECHO_DELAY)/third_party/audrey-ii \
	-I$(SC_REVERB)/include -I$(STEP_SEQ)/include -I$(LGPL_DIR)/Source -I../../DaisySP/Source/Utility
include ../../common/desktop.mk
LDFLAGS += -Tsrc/memory.ld
$(BUILD_DIR)/$(TARGET).elf: src/memory.ld

.PHONY: test test-sanitize test-thread version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror
# DaisySP headers as system headers: their warnings are not ours.
HOST_INCLUDES = -D_USE_MATH_DEFINES -Iinclude -I$(ECHO_DELAY)/include -I$(SC_REVERB)/include -isystem $(AUDREY_II) \
	-isystem $(AUDREY_DSP) -isystem $(ECHO_DELAY)/third_party/audrey-ii -isystem $(DAISYSP_DIR)/Source \
	-isystem $(DAISYSP_DIR)/Source/Utility -isystem $(LGPL_DIR)/Source
HOST_THIRD_PARTY = $(AUDREY_DSP)/BiquadFilters.cpp $(DAISYSP_DIR)/Source/Effects/overdrive.cpp \
	$(DAISYSP_DIR)/Source/Dynamics/crossfade.cpp $(LGPL_SOURCES)
# Third-party code is built with the same flags, its warnings silenced; ours stay errors.
test:
	mkdir -p build/host
	cd build/host && $(HOST_CXX) $(HOST_FLAGS) -w $(addprefix -I../../,include $(AUDREY_II) $(AUDREY_DSP)) -D_USE_MATH_DEFINES 		-I../../$(DAISYSP_DIR)/Source -I../../$(DAISYSP_DIR)/Source/Utility -I../../$(LGPL_DIR)/Source 		-c $(addprefix ../../,$(HOST_THIRD_PARTY))
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_audrey.cpp build/host/*.o -o build/host/test_audrey
	./build/host/test_audrey

# DaisySP's WhiteNoise generator relies on signed overflow; that is its own code, not ours.
test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize=signed-integer-overflow -fno-sanitize-recover=all"

# The hand-over is the shared SPSC queue, tested in Step Seq; no threads of its own.
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=thread"

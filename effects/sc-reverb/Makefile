.DEFAULT_GOAL := all
# Component version (the reverb core) and standalone firmware version are separate.
VERSION := $(shell sed -n 's/.*SC_REVERB_FIRMWARE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = ScReverbFx-v$(VERSION)
STEP_SEQ = ../../sequencers/step-seq
# ReverbSc is LGPL-2.1 (README.md). The module is pinned by DaisySP; it is not part of
# libdaisysp.a here, so only the projects that need it compile it.
LGPL_DIR = ../../DaisySP/DaisySP-LGPL
ifneq ($(MAKECMDGOALS),version)
$(if $(wildcard $(LGPL_DIR)/Source/Effects/reverbsc.cpp),,$(error DaisySP-LGPL is missing: git -C DaisySP submodule update --init DaisySP-LGPL))
endif
LGPL_SOURCES = $(LGPL_DIR)/Source/Effects/reverbsc.cpp
CPP_SOURCES = src/main.cpp $(LGPL_SOURCES)
C_INCLUDES = -Iinclude -I$(STEP_SEQ)/include -I$(LGPL_DIR)/Source -I../../DaisySP/Source/Utility
include ../../common/desktop.mk

.PHONY: test test-sanitize test-thread version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror
# DaisySP headers as system headers: their warnings are not ours.
HOST_INCLUDES = -D_USE_MATH_DEFINES -Iinclude -isystem $(DAISYSP_DIR)/Source -isystem $(DAISYSP_DIR)/Source/Utility \
	-isystem $(LGPL_DIR)/Source
# Third-party code is built with the same flags, its warnings silenced; ours stay errors.
test:
	mkdir -p build/host
	cd build/host && $(HOST_CXX) $(HOST_FLAGS) -w -D_USE_MATH_DEFINES -I../../$(DAISYSP_DIR)/Source \
		-I../../$(DAISYSP_DIR)/Source/Utility -I../../$(LGPL_DIR)/Source -c ../../$(LGPL_SOURCES)
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_sc_reverb.cpp build/host/reverbsc.o -o build/host/test_sc_reverb
	./build/host/test_sc_reverb
	$(HOST_CXX) $(HOST_FLAGS) -I$(STEP_SEQ)/tests/fakes $(HOST_INCLUDES) -I$(STEP_SEQ)/include -I$(BOOTLOADER_DIR) \
		-I$(LIBDAISY_DIR)/src tests/test_firmware.cpp build/host/reverbsc.o -o build/host/test_firmware
	./build/host/test_firmware

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# No threads of its own: the only hand-over is one atomic for the LEDs.
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=thread"

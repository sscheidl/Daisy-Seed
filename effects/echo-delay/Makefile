.DEFAULT_GOAL := all
# Component version (the echo core) and standalone firmware version are separate.
VERSION := $(shell sed -n 's/.*ECHO_DELAY_FIRMWARE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = EchoDelayFx-v$(VERSION)
AUDREY_II = third_party/audrey-ii
AUDREY_DSP = ../../common/dsp/audrey-ii
STEP_SEQ = ../../sequencers/step-seq
CPP_SOURCES = src/main.cpp $(AUDREY_DSP)/BiquadFilters.cpp
C_INCLUDES = -Iinclude -I$(AUDREY_II) -I$(AUDREY_DSP) -I$(STEP_SEQ)/include -I../../DaisySP/Source/Utility
include ../../common/desktop.mk

.PHONY: test test-sanitize test-thread version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror
# Audrey II and DaisySP as system headers: their warnings are not ours.
HOST_INCLUDES = -D_USE_MATH_DEFINES -Iinclude -isystem $(AUDREY_II) -isystem $(AUDREY_DSP) \
	-isystem $(DAISYSP_DIR)/Source -isystem $(DAISYSP_DIR)/Source/Utility
# Third-party code is built with the same flags, its warnings silenced; ours stay errors.
test:
	mkdir -p build/host
	cd build/host && $(HOST_CXX) $(HOST_FLAGS) -w -D_USE_MATH_DEFINES -I../../$(AUDREY_DSP) -I../../$(DAISYSP_DIR)/Source \
		-I../../$(DAISYSP_DIR)/Source/Utility -c ../../$(AUDREY_DSP)/BiquadFilters.cpp
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_echo_delay.cpp build/host/BiquadFilters.o -o build/host/test_echo_delay
	./build/host/test_echo_delay
	$(HOST_CXX) $(HOST_FLAGS) -I$(STEP_SEQ)/tests/fakes $(HOST_INCLUDES) -I$(STEP_SEQ)/include -I$(BOOTLOADER_DIR) \
		-I$(LIBDAISY_DIR)/src tests/test_firmware.cpp build/host/BiquadFilters.o -o build/host/test_firmware
	./build/host/test_firmware

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# No threads of its own: the only hand-over is one atomic for the LEDs.
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=thread"

.DEFAULT_GOAL := all
# Component version (tape_fx.h) and standalone firmware version are separate.
VERSION := $(shell sed -n 's/.*TAPE_FX_FIRMWARE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = TapeFx-v$(VERSION)
CPP_SOURCES = src/main.cpp
include ../../common/desktop.mk

.PHONY: test test-sanitize test-thread version bench
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) tests/test_tape_fx.cpp -o build/host/test_tape_fx
	./build/host/test_tape_fx
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes -I$(BOOTLOADER_DIR) -I$(LIBDAISY_DIR)/src tests/test_firmware.cpp -o build/host/test_firmware
	./build/host/test_firmware

test-sanitize:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# No threads of its own: the firmware keeps controls and DSP in the audio callback.
test-thread:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

# Host timing of the DSP stages (relative costs only, not a Seed3 CPU measurement).
bench:
	mkdir -p build/host
	$(HOST_CXX) -std=c++17 -O3 -Wall -Wextra -Werror tests/bench_tape_fx.cpp -o build/host/bench_tape_fx
	./build/host/bench_tape_fx

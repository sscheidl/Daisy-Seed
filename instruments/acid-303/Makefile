.DEFAULT_GOAL := all
# Component version (the voice core) and standalone firmware version are separate.
VERSION := $(shell sed -n 's/.*ACID_303_FIRMWARE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = Acid303-v$(VERSION)
CPP_SOURCES = src/main.cpp
STEP_SEQ = ../../sequencers/step-seq
C_INCLUDES = -Iinclude -I$(STEP_SEQ)/include
include ../../common/desktop.mk

.PHONY: test test-sanitize test-thread version bench fixture daisysp-host
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
# DaisySP headers as system headers: their warnings are not ours (GCC -Wextra).
HOST_INCLUDES = -D_USE_MATH_DEFINES -Iinclude -I$(STEP_SEQ)/include -I$(BOOTLOADER_DIR) -isystem $(DAISYSP_DIR)/Source -isystem $(DAISYSP_DIR)/Source/Utility
# DaisySP itself is third-party code: built with the same flags, its warnings silenced.
DAISYSP_HOST = $(DAISYSP_DIR)/Source/Filters/ladder.cpp $(DAISYSP_DIR)/Source/Synthesis/oscillator.cpp
DAISYSP_OBJECTS = build/host/ladder.o build/host/oscillator.o
daisysp-host:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -w $(HOST_INCLUDES) -c $(DAISYSP_DIR)/Source/Filters/ladder.cpp -o build/host/ladder.o
	$(HOST_CXX) $(HOST_FLAGS) -w $(HOST_INCLUDES) -c $(DAISYSP_DIR)/Source/Synthesis/oscillator.cpp -o build/host/oscillator.o
test: daisysp-host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_acid_303.cpp $(DAISYSP_OBJECTS) -o build/host/test_acid_303
	./build/host/test_acid_303
	$(HOST_CXX) $(HOST_FLAGS) -I$(STEP_SEQ)/tests/fakes $(HOST_INCLUDES) -I$(LIBDAISY_DIR)/src tests/test_firmware.cpp $(DAISYSP_OBJECTS) -o build/host/test_firmware
	./build/host/test_firmware

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# No threads of its own beyond the main/callback hand-over of Step Seq (tested there).
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

# Rewrites tests/fixtures/acid-seq/acid303_phrase.f32 (only on purpose: new voice version).
fixture: daisysp-host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_acid_303.cpp $(DAISYSP_OBJECTS) -o build/host/test_acid_303
	./build/host/test_acid_303 --write-fixture

# Host timing of the voice (relative costs only, not a Seed3 CPU measurement).
bench: daisysp-host
	$(HOST_CXX) -std=c++17 -O3 $(HOST_INCLUDES) tests/bench_acid_303.cpp $(DAISYSP_OBJECTS) -o build/host/bench_acid_303
	./build/host/bench_acid_303

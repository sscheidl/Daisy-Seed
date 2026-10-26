.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*TOY_PIANO_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
AMBIENT ?= 0
TAPE ?= 0
ifeq ($(TAPE),1)
VERSION := $(shell sed -n 's/.*TOY_PIANO_TAPE_VERSION "\(.*\)".*/\1/p' src/composed_version.h | head -n 1)
TARGET = ToyPianoTape-v$(VERSION)
override BUILD_DIR = build/tape
FX_DEFS = -DTOY_PIANO_TAPE=1 -DMOD_ON_DAISY_EXTENSIONS=1
C_DEFS = $(FX_DEFS)
else ifeq ($(AMBIENT),1)
VERSION := $(shell sed -n 's/.*TOY_PIANO_AMBIENT_VERSION "\(.*\)".*/\1/p' src/composed_version.h | head -n 1)
TARGET = ToyPianoAmbient-v$(VERSION)
override BUILD_DIR = build/ambient
FX_DEFS = -DTOY_PIANO_FX=1 -DMOD_ON_DAISY_EXTENSIONS=1
C_DEFS = $(FX_DEFS)
FX_SOURCES = ../../effects/reverb/src/ambient_reverb.cpp ../../effects/reverb/src/wet_path.cpp
else
TARGET = ToyPiano-v$(VERSION)
# v0.4: all Mod on Daisy extensions as in Physical Modeling MOD (extended monitor with
# pot positions, LFO cross modulation) and with them the echo guard (PROTOCOL.md § 2, 2a).
MOD_DEFS = -DMOD_ON_DAISY_EXTENSIONS=1
C_DEFS = $(MOD_DEFS)
endif
CPP_SOURCES = src/main.cpp src/toy_piano.cpp $(FX_SOURCES)
MOD_ON_DAISY = ../../modules/mod-on-daisy
C_INCLUDES = -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
include ../../common/desktop.mk
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
ifeq ($(TAPE),1)
test:
	mkdir -p build/host/tape
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) $(FX_DEFS) tests/test_tape.cpp src/toy_piano.cpp -o build/host/tape/test_tape
	./build/host/tape/test_tape
else ifeq ($(AMBIENT),1)
test:
	mkdir -p build/host/ambient
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) $(FX_DEFS) tests/test_ambient.cpp src/toy_piano.cpp $(FX_SOURCES) -o build/host/ambient/test_ambient
	./build/host/ambient/test_ambient
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

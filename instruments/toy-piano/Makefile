.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*TOY_PIANO_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = ToyPiano-v$(VERSION)
CPP_SOURCES = src/main.cpp src/toy_piano.cpp
MOD_ON_DAISY = ../../modules/mod-on-daisy
# v0.4: echo guard of the Mod on Daisy controller (PROTOCOL.md § 2, step 2a), no other extensions.
MOD_DEFS = -DMOD_ON_DAISY_IGNORE_REPLIES=1
C_DEFS = $(MOD_DEFS)
C_INCLUDES = -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
include ../../common/desktop.mk

.PHONY: test test-sanitize test-thread version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
HOST_INCLUDES = $(MOD_DEFS) -Isrc -I$(DAISYSP_DIR)/Source -I$(LIBDAISY_DIR)/src -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
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

test-sanitize:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

test-thread:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

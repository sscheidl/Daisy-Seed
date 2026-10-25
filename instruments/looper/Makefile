.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*LOOPER_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = Looper-v$(VERSION)
STORE_SOURCES = src/wav.cpp src/resample.cpp src/loop_store.cpp
CPP_SOURCES = src/main.cpp src/looper_engine.cpp $(STORE_SOURCES) src/fatfs_volume.cpp
USE_FATFS = 1
C_INCLUDES += -Isrc
include ../../common/desktop.mk

.PHONY: test test-sanitize version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
# The callback test runs the production main.cpp with a 4 s buffer and a card in memory.
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_engine.cpp src/looper_engine.cpp -o build/host/test_engine
	./build/host/test_engine
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_wav.cpp src/wav.cpp -o build/host/test_wav
	./build/host/test_wav
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_resample.cpp src/resample.cpp -o build/host/test_resample
	./build/host/test_resample
	$(HOST_CXX) $(HOST_FLAGS) -Isrc -Itests/fakes tests/test_store.cpp src/looper_engine.cpp $(STORE_SOURCES) -o build/host/test_store
	./build/host/test_store
	$(HOST_CXX) $(HOST_FLAGS) -Isrc -Itests/fakes -I$(BOOTLOADER_DIR) -I$(LIBDAISY_DIR)/src -DLOOPER_BUFFER_FRAMES=192000 tests/test_callback.cpp src/looper_engine.cpp $(STORE_SOURCES) -o build/host/test_callback
	./build/host/test_callback

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

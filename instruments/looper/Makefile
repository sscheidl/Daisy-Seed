.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*LOOPER_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = Looper-v$(VERSION)
CPP_SOURCES = src/main.cpp src/looper_engine.cpp
C_INCLUDES += -Isrc
include ../../common/desktop.mk

.PHONY: test test-sanitize version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
# The callback test runs the production main.cpp with a 4 s buffer.
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_engine.cpp src/looper_engine.cpp -o build/host/test_engine
	./build/host/test_engine
	$(HOST_CXX) $(HOST_FLAGS) -Isrc -Itests/fakes -I$(LIBDAISY_DIR)/src -DLOOPER_BUFFER_FRAMES=192000 tests/test_callback.cpp src/looper_engine.cpp -o build/host/test_callback
	./build/host/test_callback

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

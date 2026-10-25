.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*AMBIENT_REVERB_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = AmbientReverb-v$(VERSION)
CPP_SOURCES = src/main.cpp src/ambient_reverb.cpp src/wet_path.cpp
include ../../common/desktop.mk

.PHONY: test test-sanitize version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) tests/ambient_verify.cpp src/ambient_reverb.cpp src/wet_path.cpp -o build/host/ambient_verify
	./build/host/ambient_verify
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes -I$(BOOTLOADER_DIR) -I$(LIBDAISY_DIR)/src tests/test_controls.cpp src/ambient_reverb.cpp src/wet_path.cpp -o build/host/test_controls
	./build/host/test_controls

test-sanitize:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

.DEFAULT_GOAL := all
# Component version (the Filter FX core) and standalone firmware version are separate.
VERSION := $(shell sed -n 's/.*FILTER_FX_FIRMWARE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = FilterFx-v$(VERSION)
CPP_SOURCES = src/main.cpp
STEP_SEQ = ../../sequencers/step-seq
C_INCLUDES = -Iinclude -I$(STEP_SEQ)/include
include ../../common/desktop.mk

.PHONY: test test-sanitize test-thread version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
HOST_INCLUDES = -Iinclude -I$(STEP_SEQ)/include -I$(BOOTLOADER_DIR)
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_filter_fx.cpp -o build/host/test_filter_fx
	./build/host/test_filter_fx
	$(HOST_CXX) $(HOST_FLAGS) -I$(STEP_SEQ)/tests/fakes $(HOST_INCLUDES) -I$(LIBDAISY_DIR)/src tests/test_firmware.cpp -o build/host/test_firmware
	./build/host/test_firmware

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# No threads of its own beyond the main/callback hand-over of Step Seq (tested there).
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

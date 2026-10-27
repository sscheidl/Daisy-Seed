.DEFAULT_GOAL := all
# Component version (the Drive core) and standalone firmware version are separate.
VERSION := $(shell sed -n 's/.*DRIVE_FIRMWARE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = DriveFx-v$(VERSION)
CPP_SOURCES = src/main.cpp
STEP_SEQ = ../../sequencers/step-seq
C_INCLUDES = -Iinclude -I$(STEP_SEQ)/include
include ../../common/desktop.mk

.PHONY: test test-sanitize test-thread version autolevel-table
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
HOST_INCLUDES = -Iinclude -I$(STEP_SEQ)/include -I$(BOOTLOADER_DIR)
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_drive.cpp -o build/host/test_drive
	./build/host/test_drive
	$(HOST_CXX) $(HOST_FLAGS) -I$(STEP_SEQ)/tests/fakes $(HOST_INCLUDES) -I$(LIBDAISY_DIR)/src tests/test_firmware.cpp -o build/host/test_firmware
	./build/host/test_firmware

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# No threads of its own beyond the main/callback hand-over of Step Seq (tested there).
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

# Rewrites include/drive/autolevel_table.h from the phase 0 fixtures (only on purpose).
autolevel-table:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tools/gen_autolevel.cpp -o build/host/gen_autolevel
	./build/host/gen_autolevel

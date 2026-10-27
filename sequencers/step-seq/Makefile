.DEFAULT_GOAL := all
# Component version (the core) and standalone firmware version are separate.
VERSION := $(shell sed -n 's/.*STEP_SEQ_FIRMWARE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = StepSeq-v$(VERSION)
CPP_SOURCES = src/main.cpp
C_INCLUDES = -Iinclude
include ../../common/desktop.mk

.PHONY: test test-sanitize test-thread version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
HOST_INCLUDES = -Iinclude -I$(BOOTLOADER_DIR) -DCONTRACTS_DIR='"contracts"'
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_step_seq.cpp -o build/host/test_step_seq
	./build/host/test_step_seq
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) -I$(LIBDAISY_DIR)/src tests/test_firmware.cpp -o build/host/test_firmware
	./build/host/test_firmware
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_thread.cpp -o build/host/test_thread
	./build/host/test_thread
	python3 tools/gen_params.py --check
	python3 tools/sq_probe.py selftest

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# Linux only (TSan): the main/callback hand-over (SPSC queue, triple buffer, note feed).
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

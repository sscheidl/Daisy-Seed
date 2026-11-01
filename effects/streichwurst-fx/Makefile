.DEFAULT_GOAL := all
# Component version (include/streichwurst_fx/version.h) and the standalone FX firmware
# version (src/version.h, make PROJECT=streichwurst-fx) are separate.
VERSION := $(shell sed -n 's/.*STREICHWURST_FX_FIRMWARE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = StreichwurstFx-v$(VERSION)
CPP_SOURCES = src/main.cpp
STEP_SEQ = ../../sequencers/step-seq
# The shared Streichwurst parameter contract and register (generated table, FX entries).
STREICHWURST = ../../instruments/streichwurst
C_INCLUDES = -Iinclude -I$(STEP_SEQ)/include -I$(STREICHWURST)/include -I$(STREICHWURST)/src
include ../../common/desktop.mk

.PHONY: test test-fx test-firmware test-sanitize test-thread version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
HOST_INCLUDES = -D_USE_MATH_DEFINES -Iinclude -I$(STEP_SEQ)/include -I$(STREICHWURST)/include -I$(STREICHWURST)/src \
	-I$(BOOTLOADER_DIR)
test: test-fx test-firmware
test-fx:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -D_USE_MATH_DEFINES -Iinclude tests/test_fx.cpp -o build/host/test_fx
	./build/host/test_fx
# The production src/main.cpp on the Step Seq host fakes.
test-firmware:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -I$(STEP_SEQ)/tests/fakes $(HOST_INCLUDES) -I$(LIBDAISY_DIR)/src tests/test_firmware.cpp \
		-o build/host/test_firmware
	./build/host/test_firmware

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# No threads of its own beyond the main/callback hand-over of Step Seq (tested there).
test-thread:
	"$(MAKE)" test-firmware HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

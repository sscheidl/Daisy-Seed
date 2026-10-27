.DEFAULT_GOAL := all
# Product version (src/composed_version.h); the components keep their own versions.
VERSION := $(shell sed -n 's/.*ACID_SEQ_VERSION "\(.*\)".*/\1/p' src/composed_version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/composed_version.h))
TARGET = AcidSeq-v$(VERSION)
CPP_SOURCES = src/main.cpp
STEP_SEQ = ../step-seq
ACID_303 = ../../instruments/acid-303
DRIVE = ../../effects/drive
DELAY = ../../effects/stereo-delay
C_INCLUDES = -Iinclude -I$(STEP_SEQ)/include -I$(ACID_303)/include -I$(DRIVE)/include -I$(DELAY)/include
include ../../common/desktop.mk

.PHONY: test test-sanitize test-thread version daisysp-host
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
# DaisySP headers as system headers: their warnings are not ours (GCC -Wextra).
HOST_INCLUDES = -D_USE_MATH_DEFINES -Iinclude -I$(STEP_SEQ)/include -I$(ACID_303)/include -I$(DRIVE)/include \
	-I$(DELAY)/include -I$(BOOTLOADER_DIR) -isystem $(DAISYSP_DIR)/Source -isystem $(DAISYSP_DIR)/Source/Utility
DAISYSP_OBJECTS = build/host/ladder.o build/host/oscillator.o
daisysp-host:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -w $(HOST_INCLUDES) -c $(DAISYSP_DIR)/Source/Filters/ladder.cpp -o build/host/ladder.o
	$(HOST_CXX) $(HOST_FLAGS) -w $(HOST_INCLUDES) -c $(DAISYSP_DIR)/Source/Synthesis/oscillator.cpp -o build/host/oscillator.o
test: daisysp-host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_acid_seq.cpp $(DAISYSP_OBJECTS) -o build/host/test_acid_seq
	./build/host/test_acid_seq
	$(HOST_CXX) $(HOST_FLAGS) -I$(STEP_SEQ)/tests/fakes $(HOST_INCLUDES) -I$(LIBDAISY_DIR)/src tests/test_firmware.cpp $(DAISYSP_OBJECTS) -o build/host/test_firmware
	./build/host/test_firmware

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# The main/callback hand-over is Step Seq's (tested there with TSan); this adds one more
# SPSC queue of the same type.
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

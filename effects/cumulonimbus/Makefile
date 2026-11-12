.DEFAULT_GOAL := all
# Component version (src/version.h CUMULONIMBUS_VERSION) and standalone firmware version are separate.
# RATE=48 (default): 48 kHz grain kernel. RATE=32: 32 kHz grain kernel behind a 48<->32 kHz converter.
RATE ?= 48
$(if $(filter $(RATE),48 32),,$(error RATE must be 48 or 32))
VERSION := $(shell sed -n 's/.*CUMULONIMBUS_FX_FIRMWARE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = CumulonimbusFx$(RATE)-v$(VERSION)
override BUILD_DIR = build/fx$(RATE)
TORUS_INCLUDES = -I../../instruments/physical-modeling/torus -I../../instruments/physical-modeling/third_party
HOST_TORUS = -DTEST -isystem ../../instruments/physical-modeling/torus -isystem ../../instruments/physical-modeling/third_party
C_DEFS = -DCUMULONIMBUS_INTERNAL_RATE=$(RATE)000
C_INCLUDES = $(TORUS_INCLUDES)
CPP_SOURCES = src/main.cpp
include ../../common/desktop.mk
# The Clouds/grain state and the reverb memory live in AXI SRAM behind the image (NOLOAD),
# the PCM rings in SDRAM; see the link-map summary in the run sheet.
LDSCRIPT := ../../instruments/toy-piano/ld/toy_piano_axi.lds
$(BUILD_DIR)/$(TARGET).elf: $(LDSCRIPT)

.PHONY: test test-sanitize test-thread version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
PYTHON ?= python3
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
HOST_DEFS = -D_USE_MATH_DEFINES
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_DEFS) $(HOST_TORUS) -DCUMULONIMBUS_INTERNAL_RATE=48000 tests/test_cumulonimbus.cpp -o build/host/test_cumulonimbus48
	./build/host/test_cumulonimbus48
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_DEFS) $(HOST_TORUS) -DCUMULONIMBUS_INTERNAL_RATE=32000 tests/test_cumulonimbus.cpp -o build/host/test_cumulonimbus32
	./build/host/test_cumulonimbus32
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_DEFS) $(HOST_TORUS) tests/test_firmware.cpp -o build/host/test_firmware48
	./build/host/test_firmware48
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_DEFS) $(HOST_TORUS) -DCUMULONIMBUS_INTERNAL_RATE=32000 tests/test_firmware.cpp -o build/host/test_firmware32
	./build/host/test_firmware32
	$(PYTHON) -m unittest discover -s tools -p test_cumulonimbus_probe.py -v

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# The meter hands snapshots between the audio callback and the main loop through atomics.
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

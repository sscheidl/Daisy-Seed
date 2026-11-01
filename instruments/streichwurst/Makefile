.DEFAULT_GOAL := all
# Product version (src/version.h); the instrument and FX components keep their own versions.
VERSION := $(shell sed -n 's/.*STREICHWURST_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = Streichwurst-v$(VERSION)
CPP_SOURCES = src/main.cpp
FX = ../../effects/streichwurst-fx
STEP_SEQ = ../../sequencers/step-seq
MOD_ON_DAISY = ../../modules/mod-on-daisy
# Mod on Daisy with the extended monitor and LFO cross modulation (PROTOCOL.md 3.6).
C_DEFS += -DMOD_ON_DAISY_EXTENSIONS=1
C_INCLUDES = -Iinclude -Isrc -I$(FX)/include -I$(STEP_SEQ)/include -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
include ../../common/desktop.mk

.PHONY: test test-instrument test-product test-firmware test-sanitize test-thread version compare-rates render
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
HOST_INCLUDES = -D_USE_MATH_DEFINES -Iinclude -Isrc -I$(FX)/include -I$(STEP_SEQ)/include
HOST_MOD = -DMOD_ON_DAISY_EXTENSIONS=1 -I$(MOD_ON_DAISY)/include -I$(MOD_ON_DAISY)/adapters
test: test-instrument test-product test-firmware
test-instrument:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_instrument.cpp -o build/host/test_instrument
	./build/host/test_instrument
test-product:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) -I../../effects/stereo-delay/include tests/test_product.cpp -o build/host/test_product
	./build/host/test_product
# The production src/main.cpp on host fakes (desktop kit, USB MIDI, Mod on Daisy adapter).
test-firmware:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes -I$(STEP_SEQ)/tests/fakes $(HOST_INCLUDES) $(HOST_MOD) -I$(BOOTLOADER_DIR) \
		-I$(LIBDAISY_DIR)/src tests/test_firmware.cpp -o build/host/test_firmware
	./build/host/test_firmware

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# The main/callback hand-over uses Step Seq's SPSC queue and triple buffer (tested there with
# TSan) plus one atomic counter; this runs the firmware test under TSan where available.
test-thread:
	"$(MAKE)" test-firmware HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

# Own 40 kHz core + own resampler against the direct 48 kHz core (docs/RATE_COMPARISON.md).
compare-rates:
	mkdir -p build/host
	$(HOST_CXX) -std=c++17 -O2 -Wall -Wextra -Werror $(HOST_INCLUDES) tools/compare_rates.cpp -o build/host/compare_rates
	./build/host/compare_rates

# Own offline test signals as WAV files under build/render (not original renders).
render:
	mkdir -p build/host build/render
	$(HOST_CXX) -std=c++17 -O2 -Wall -Wextra -Werror $(HOST_INCLUDES) tools/render.cpp -o build/host/render
	./build/host/render build/render

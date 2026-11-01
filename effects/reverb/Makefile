.DEFAULT_GOAL := all
ifneq ($(filter-out undefined,$(origin AMB_GATE) $(origin AMB_WET32)),)
$(error AMB_GATE/AMB_WET32 are obsolete; shared Ambient 0.4 always uses gating and 32-12 kHz)
endif
VERSION := $(shell sed -n 's/.*AMBIENT_REVERB_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
TARGET = AmbientReverb-v$(VERSION)
CPP_SOURCES = src/main.cpp src/ambient_reverb.cpp src/wet_path.cpp
C_INCLUDES = -I../../sequencers/step-seq/include
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
	$(HOST_CXX) $(HOST_FLAGS) tests/test_performance.cpp src/ambient_reverb.cpp src/wet_path.cpp -o build/host/test_performance
	./build/host/test_performance
	$(HOST_CXX) $(HOST_FLAGS) tests/test_variants.cpp src/ambient_reverb.cpp src/wet_path.cpp -o build/host/test_variants
	./build/host/test_variants
	$(HOST_CXX) $(HOST_FLAGS) -I../../sequencers/step-seq/include -Itests/fakes -I$(BOOTLOADER_DIR) -I$(LIBDAISY_DIR)/src tests/test_controls.cpp src/ambient_reverb.cpp src/wet_path.cpp -o build/host/test_controls
	./build/host/test_controls

test-sanitize:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

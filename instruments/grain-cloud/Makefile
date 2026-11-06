.DEFAULT_GOAL := test
# Host core only: there is no firmware product yet, so this project has no `all` image.
# Stage 3 (Daisy adapter) adds the firmware rules from common/desktop.mk.
VERSION := $(shell sed -n 's/.*GRAIN_CLOUD_VERSION "\(.*\)".*/\1/p' include/grain_cloud/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from include/grain_cloud/version.h))
STEP_SEQ = ../../sequencers/step-seq
MOD_ON_DAISY = ../../modules/mod-on-daisy

.PHONY: test test-units test-engine test-modulation test-rt-scan test-sanitize test-thread cost version
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread
HOST_INCLUDES = -D_USE_MATH_DEFINES -Iinclude -I$(STEP_SEQ)/include -I$(MOD_ON_DAISY)/include

test: test-rt-scan test-units test-engine test-modulation

# R3 by code review, kept as a scan: the per-sample path has no transcendental call, no
# division and no allocation. Control-rate headers are not in this list.
RT_FILES = include/grain_cloud/grain_renderer.h
test-rt-scan:
	@! grep -nE '\b(tanf?|expf?|exp2f?|powf?|cosf?|sinf?|logf?|log2f?|sqrtf?|new|malloc)\b *\(|[^/*]/[^/*]|%' $(RT_FILES) \
		| grep -vE '^[0-9]+:\s*//' | grep -vE '^[0-9]+:(constexpr float kQ24|.*#include)'
	@echo "rt scan ok"

test-units:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_units.cpp -o build/host/test_units
	./build/host/test_units
test-engine:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) -I../../common/audio tests/test_engine.cpp -o build/host/test_engine
	./build/host/test_engine
test-modulation:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_modulation.cpp -o build/host/test_modulation
	./build/host/test_modulation

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# One producer thread against the audio side, on the shared queue and triple buffer.
test-thread:
	"$(MAKE)" test-engine HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

# Operation counts and assumed cycles. Every cycle figure is NOT MEASURED.
cost:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tools/cost_estimate.cpp -o build/host/cost_estimate
	./build/host/cost_estimate

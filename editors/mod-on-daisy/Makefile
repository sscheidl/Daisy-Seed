# Editor core tests (no JUCE), the JUCE app via CMake (see README.md).
.DEFAULT_GOAL := test
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -Wshadow -pthread
INCLUDES = -I../../modules/mod-on-daisy/include -I../../common/bootloader -DMOD_ON_DAISY_EXTENSIONS=1 # monitor extensions, LFO cross modulation
HEADERS = $(wildcard src/core/*.h) $(wildcard ../../sequencers/step-seq/include/step_seq/*.h) $(wildcard ../../modules/mod-on-daisy/include/mod_on_daisy/*.h) $(wildcard ../../common/bootloader/*.h)

.PHONY: test test-sanitize test-editor-sq app clean
# Streichwurst register, tables and Mod on Daisy columns (simulator and its tests).
SQ_INCLUDES = -I../../sequencers/step-seq/include -I../../instruments/streichwurst/include -I../../instruments/streichwurst/src -I../../effects/streichwurst-fx/include -D_USE_MATH_DEFINES
HEADERS += $(wildcard ../../instruments/streichwurst/src/*.h) $(wildcard ../../instruments/streichwurst/include/streichwurst/*.h)
test: $(HEADERS) tests/test_core.cpp tests/test_sq.cpp tests/test_streichwurst.cpp
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(INCLUDES) tests/test_core.cpp -o build/host/test_core
	./build/host/test_core
	$(HOST_CXX) $(HOST_FLAGS) $(INCLUDES) $(SQ_INCLUDES) tests/test_sq.cpp -o build/host/test_sq
	./build/host/test_sq
	$(HOST_CXX) $(HOST_FLAGS) $(INCLUDES) $(SQ_INCLUDES) tests/test_streichwurst.cpp -o build/host/test_streichwurst
	./build/host/test_streichwurst
	$(HOST_CXX) $(HOST_FLAGS) $(INCLUDES) $(SQ_INCLUDES) tests/test_ambient.cpp -o build/host/test_ambient
	./build/host/test_ambient
	$(HOST_CXX) $(HOST_FLAGS) $(INCLUDES) $(SQ_INCLUDES) tests/sq_sim.cpp -o build/host/sq_sim
	node web/tests/sq_protocol.test.cjs
	node web/tests/sq_client.test.cjs build/host/sq_sim
	python3 ../../sequencers/step-seq/tools/gen_params.py --check

# Browser e2e of the Acid Seq pages (Playwright with Chromium); MOD_SIM: mod_sim for the MD check.
test-editor-sq: test
	PLAYWRIGHT_MODULE=$(PLAYWRIGHT_MODULE) node web/tests/e2e_sq.cjs build/host/sq_sim $(MOD_SIM) build/host

test-sanitize:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -Wshadow -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# JUCE standalone (Linux/macOS; on Windows use the CMake commands in README.md).
app:
	cmake -S . -B build/app -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)
	cmake --build build/app --parallel

clean:
	rm -rf build

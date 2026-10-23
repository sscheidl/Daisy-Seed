# Host tests of the header-only Mod on Daisy module (TEST_PLAN_PHASE2.md § 2).
# The bench firmware has its own Makefile in bench/.
.DEFAULT_GOAL := test
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -Wshadow -pthread
HOST_INCLUDES = -Iinclude -Itests
STRESS_FLAGS = -DMOD_ON_DAISY_MAX_ACTIVE=64
HEADERS = $(wildcard include/mod_on_daisy/*.h) tests/common.h bench/src/scenarios.h

.PHONY: test test-sanitize test-thread test-editor clean
test: $(HEADERS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_mod.cpp -o build/host/test_mod
	./build/host/test_mod
	$(HOST_CXX) $(HOST_FLAGS) $(STRESS_FLAGS) $(HOST_INCLUDES) tests/test_mod.cpp -o build/host/test_mod_64
	MOD_FUZZ_ROUNDS=20000 ./build/host/test_mod_64
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_bench.cpp -o build/host/test_bench
	./build/host/test_bench
	$(HOST_CXX) $(HOST_FLAGS) $(STRESS_FLAGS) $(HOST_INCLUDES) tests/test_bench.cpp -o build/host/test_bench_64
	./build/host/test_bench_64
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_thread.cpp -o build/host/test_thread
	./build/host/test_thread
	python3 tools/mod_probe.py selftest
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tools/mod_sim.cpp -o build/host/mod_sim
	node ../../editors/mod-on-daisy/web/tests/protocol.test.cjs

# Browser editor against the simulated device in headless Chromium (needs Playwright;
# preinstalled in the cloud environment, locally: npm i -g playwright && npx playwright install chromium).
PLAYWRIGHT_MODULE ?= $(shell npm root -g 2>/dev/null)/playwright
test-editor:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tools/mod_sim.cpp -o build/host/mod_sim
	PLAYWRIGHT_MODULE=$(PLAYWRIGHT_MODULE) node ../../editors/mod-on-daisy/web/tests/e2e.cjs build/host/mod_sim build/host

test-sanitize:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -Wshadow -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

test-thread:
	mkdir -p build/host
	$(HOST_CXX) -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread $(HOST_INCLUDES) tests/test_thread.cpp -o build/host/test_thread_tsan
	./build/host/test_thread_tsan

clean:
	rm -rf build

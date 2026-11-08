.DEFAULT_GOAL := test
# Host checks of the header-only filter core; it has no firmware of its own.
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -ffp-contract=off -Wall -Wextra -Werror -pthread
HOST_INCLUDES = -Iinclude

.PHONY: test test-sanitize test-thread measure cost clean
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_ladder.cpp -o build/host/test_ladder
	./build/host/test_ladder

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -ffp-contract=off -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# The core has no threads; kept so that every component answers the same targets.
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -ffp-contract=off -Wall -Wextra -Werror -pthread -fsanitize=thread"

# The tables of docs/DESIGN.md, measured on the real core.
measure:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tools/measure_ladder.cpp -o build/host/measure_ladder
	./build/host/measure_ladder

# Static cost on Cortex-M7 (instruction counts from the ARM compiler, no hardware involved).
cost:
	python3 tools/arm_cost.py tools/cost_probe.cpp -Iinclude

clean:
	rm -rf build

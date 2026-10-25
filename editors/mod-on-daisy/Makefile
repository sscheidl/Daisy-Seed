# Editor core tests (no JUCE), the JUCE app via CMake (see README.md).
.DEFAULT_GOAL := test
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -Wshadow -pthread
INCLUDES = -I../../modules/mod-on-daisy/include -I../../common/bootloader -DMOD_ON_DAISY_EXTENSIONS=1 # monitor extensions, LFO cross modulation
HEADERS = $(wildcard src/core/*.h) $(wildcard ../../modules/mod-on-daisy/include/mod_on_daisy/*.h) $(wildcard ../../common/bootloader/*.h)

.PHONY: test test-sanitize app clean
test: $(HEADERS) tests/test_core.cpp
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(INCLUDES) tests/test_core.cpp -o build/host/test_core
	./build/host/test_core

test-sanitize:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -Wshadow -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# JUCE standalone (Linux/macOS; on Windows use the CMake commands in README.md).
app:
	cmake -S . -B build/app -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)
	cmake --build build/app --parallel

clean:
	rm -rf build

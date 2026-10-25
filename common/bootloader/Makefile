# Host tests of the shared bootloader entry (header-only, no libDaisy needed).
.DEFAULT_GOAL := test
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -Wshadow -pthread
HOST_INCLUDES = -I. -Itests/fakes

.PHONY: test test-sanitize clean
test: bootloader_entry.h bootloader_daisy.h tests/test_bootloader_entry.cpp
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_bootloader_entry.cpp -o build/host/test_bootloader_entry
	./build/host/test_bootloader_entry

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -Wshadow -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

clean:
	rm -rf build

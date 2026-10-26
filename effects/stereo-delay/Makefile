# Stereo Delay: host tests only until the firmware exists (project delay-fx is planned).
.DEFAULT_GOAL := test
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror -pthread

.PHONY: test test-sanitize test-thread
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) tests/test_stereo_delay.cpp -o build/host/test_stereo_delay
	./build/host/test_stereo_delay

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

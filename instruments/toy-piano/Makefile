.DEFAULT_GOAL := all
TARGET = ToyPiano-v0.1
CPP_SOURCES = src/main.cpp src/toy_piano.cpp
include ../../common/desktop.mk

.PHONY: test test-sanitize
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror
HOST_INCLUDES = -Isrc -I$(DAISYSP_DIR)/Source -I$(LIBDAISY_DIR)/src
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_toy_piano.cpp src/toy_piano.cpp -o build/host/test_toy_piano
	./build/host/test_toy_piano
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) tests/test_callback.cpp src/toy_piano.cpp -o build/host/test_callback
	./build/host/test_callback

test-sanitize:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

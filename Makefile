.DEFAULT_GOAL := all
TARGET = ToyPiano-v0.1
DEBUG = 0
OPT = -O3
APP_TYPE = BOOT_SRAM
CPP_STANDARD = -std=gnu++17
CPP_SOURCES = src/main.cpp src/toy_piano.cpp
LIBDAISY_DIR = libDaisy
DAISYSP_DIR = DaisySP
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
BOOT_BIN = $(SYSTEM_FILES_DIR)/dsy_bootloader_v6_4-extdfu-2000ms.bin

include $(SYSTEM_FILES_DIR)/Makefile

.PHONY: libs test test-sanitize
libs:
	$(MAKE) -C $(LIBDAISY_DIR)
	$(MAKE) -C $(DAISYSP_DIR)

$(BUILD_DIR)/$(TARGET).elf: $(LIBDAISY_DIR)/build/libdaisy.a $(DAISYSP_DIR)/build/libdaisysp.a
$(LIBDAISY_DIR)/build/libdaisy.a:
	$(MAKE) -C $(LIBDAISY_DIR)
$(DAISYSP_DIR)/build/libdaisysp.a:
	$(MAKE) -C $(DAISYSP_DIR)

HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror
HOST_INCLUDES = -Isrc -IDaisySP/Source -IlibDaisy/src
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_toy_piano.cpp src/toy_piano.cpp -o build/host/test_toy_piano
	./build/host/test_toy_piano
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes $(HOST_INCLUDES) tests/test_callback.cpp src/toy_piano.cpp -o build/host/test_callback
	./build/host/test_callback

test-sanitize:
	$(MAKE) test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

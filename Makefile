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

.PHONY: libs test
libs:
	$(MAKE) -C $(LIBDAISY_DIR)
	$(MAKE) -C $(DAISYSP_DIR)

$(BUILD_DIR)/$(TARGET).elf: $(LIBDAISY_DIR)/build/libdaisy.a $(DAISYSP_DIR)/build/libdaisysp.a
$(LIBDAISY_DIR)/build/libdaisy.a:
	$(MAKE) -C $(LIBDAISY_DIR)
$(DAISYSP_DIR)/build/libdaisysp.a:
	$(MAKE) -C $(DAISYSP_DIR)

HOST_CXX ?= g++
test:
	mkdir -p build/host
	$(HOST_CXX) -std=c++17 -O2 -Wall -Wextra -Werror -pthread -Isrc -IDaisySP/Source -IlibDaisy/src tests/test_toy_piano.cpp src/toy_piano.cpp -o build/host/test_toy_piano
	./build/host/test_toy_piano

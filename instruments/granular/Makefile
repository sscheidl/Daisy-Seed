.DEFAULT_GOAL := all
VERSION := $(shell sed -n 's/.*NEBULAE_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
TARGET = NebulaeOnDaisy-v$(VERSION)
DSP_SOURCES = src/reference_dsp.cpp src/control.cpp src/engine.cpp src/wav.cpp
CPP_SOURCES = src/main.cpp $(DSP_SOURCES)
C_SOURCES = src/sd_driver.c src/heap.c
USE_FATFS = 1
C_INCLUDES += -Isrc -fstack-usage
include ../../common/desktop.mk
LDFLAGS += -Tsrc/memory.ld
$(BUILD_DIR)/$(TARGET).elf: src/memory.ld
all: $(BUILD_DIR)/SHA256SUMS
$(BUILD_DIR)/SHA256SUMS: $(BUILD_DIR)/$(TARGET).bin
	cd $(BUILD_DIR) && sha256sum $(TARGET).bin > SHA256SUMS

HOST_CXX ?= g++
HOST_FLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -pthread
.PHONY: version test test-sanitize test-thread
version:
	@echo $(VERSION)
test:
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_nebulae.cpp $(DSP_SOURCES) -o build/host/test_nebulae
	./build/host/test_nebulae
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes -Isrc -I$(BOOTLOADER_DIR) -I../../libDaisy/src tests/test_callback.cpp $(DSP_SOURCES) -o build/host/test_callback
	./build/host/test_callback
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fakes tests/test_sd_driver.cpp -o build/host/test_sd_driver
	./build/host/test_sd_driver
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_queue.cpp -o build/host/test_queue
	./build/host/test_queue
	$(HOST_CXX) $(HOST_FLAGS) -Isrc tests/test_bridge.cpp $(DSP_SOURCES) -o build/host/test_bridge
	./build/host/test_bridge
test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer"
test-thread:
	mkdir -p build/host
	$(HOST_CXX) -std=c++17 -O1 -g -pthread -fsanitize=thread -Isrc tests/test_queue.cpp -o build/host/test_queue
	./build/host/test_queue
	$(HOST_CXX) -std=c++17 -O1 -g -pthread -fsanitize=thread -Isrc tests/test_bridge.cpp $(DSP_SOURCES) -o build/host/test_bridge
	./build/host/test_bridge

# Host tests and ARM checks for the shared read-only SD driver.
# Firmware projects do not use this file; they include sd.mk.
HOST_CC ?= gcc
HOST_CXX ?= g++
PYTHON ?= python3
ARM_CC ?= arm-none-eabi-gcc
ARM_NM ?= arm-none-eabi-nm
HOST_WARN = -Wall -Wextra -Werror
SAN ?=
HOST_OPT ?= -O2
VENDOR = fatfs/ff.c fatfs/ffunicode.c
B = build/host

.PHONY: test test-sanitize arm-check clean
test:
	mkdir -p $(B)/mkimage $(B)/driver
	$(HOST_CC) -std=gnu11 $(HOST_OPT) $(SAN) -Itests/mkfs -Ifatfs tests/mkimage.c $(VENDOR) -o $(B)/mkimage/mkimage
	./$(B)/mkimage/mkimage fat32 $(B)/fat32.img
	./$(B)/mkimage/mkimage exfat $(B)/exfat.img
	$(HOST_CC) -std=gnu11 $(HOST_OPT) $(SAN) -I. -Ifatfs -c fatfs/ff.c -o $(B)/driver/ff.o
	$(HOST_CC) -std=gnu11 $(HOST_OPT) $(SAN) -I. -Ifatfs -c fatfs/ffunicode.c -o $(B)/driver/ffunicode.o
	$(HOST_CC) -std=gnu11 $(HOST_OPT) $(SAN) $(HOST_WARN) -I. -Ifatfs -Itests/fakes -c ro_sd.c -o $(B)/driver/ro_sd.o
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(SAN) $(HOST_WARN) -I. -Ifatfs -Itests/fakes tests/test_ro_sd.cpp \
		$(B)/driver/ro_sd.o $(B)/driver/ff.o $(B)/driver/ffunicode.o -o $(B)/test_ro_sd
	./$(B)/test_ro_sd $(B)/fat32.img $(B)/exfat.img

test-sanitize:
	"$(MAKE)" test HOST_OPT="-O1 -g" SAN="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# Compiles the driver exactly as a firmware build would (Cortex-M7, libDaisy HAL
# headers) and applies the shared symbol policy to the objects.
LIBDAISY_DIR ?= ../../libDaisy
ARM_FLAGS = -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -O2 -Wall \
	-fdata-sections -ffunction-sections -DUSE_HAL_DRIVER -DSTM32H750xx -DCORE_CM7 \
	-DSTM32H750IB -DUSE_FULL_LL_DRIVER -include stm32h7xx.h \
	-I. -Ifatfs -I$(LIBDAISY_DIR)/src -I$(LIBDAISY_DIR)/src/sys \
	-I$(LIBDAISY_DIR)/Drivers/CMSIS_5/CMSIS/Core/Include \
	-I$(LIBDAISY_DIR)/Drivers/CMSIS-Device/ST/STM32H7xx/Include \
	-I$(LIBDAISY_DIR)/Drivers/STM32H7xx_HAL_Driver/Inc
A = build/arm
arm-check:
	mkdir -p $(A)
	$(ARM_CC) $(ARM_FLAGS) -Werror -c ro_sd.c -o $(A)/ro_sd.o
	$(ARM_CC) $(ARM_FLAGS) -c fatfs/ff.c -o $(A)/ff.o
	$(ARM_CC) $(ARM_FLAGS) -c fatfs/ffunicode.c -o $(A)/ffunicode.o
	$(PYTHON) ../../scripts/check-sd-symbols.py --nm $(ARM_NM) \
		--require disk_read,disk_initialize,disk_status,f_mount,f_open,f_read,f_readdir \
		$(A)/ro_sd.o $(A)/ff.o $(A)/ffunicode.o
	printf 'int HAL_SD_WriteBlocks(void*,void*,unsigned,unsigned,unsigned);\nint probe(void){return HAL_SD_WriteBlocks(0,0,0,1,1);}\n' > $(A)/inject.c
	$(ARM_CC) -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -O2 -c $(A)/inject.c -o $(A)/inject.o
	! $(PYTHON) ../../scripts/check-sd-symbols.py --nm $(ARM_NM) --require f_mount \
		$(A)/ff.o $(A)/inject.o > $(A)/inject.log 2>&1
	grep -q HAL_SD_WriteBlocks $(A)/inject.log
	@echo "PASS common/sd arm-check: read-only objects, injected write detected"

clean:
	rm -rf build

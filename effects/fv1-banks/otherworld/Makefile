# Component fv1-bank-otherworld: Otherworld vol. 1 programs (MIT) for effects/fv1-vm.
#   make bank        regenerate include/fv1bank/otherworld_programs.h and otherworld_programs.js
#   make test        bank tests (fresh build, release match) and the fv1-vm consumer run of all programs
.PHONY: bank test test-sanitize test-thread version
VERSION := $(shell sed -n 's/.*SEED_FV1_BANK_OTHERWORLD_VERSION "\(.*\)".*/\1/p' include/fv1bank/otherworld_version.h)
version:
	@echo $(VERSION)
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror
HOST_INCLUDES = -Iinclude -I../../fv1-vm/include
PYTHON ?= python3

bank:
	"$(PYTHON)" tools/build_bank.py

test:
	"$(PYTHON)" -m unittest discover -s tests -p test_bank.py -v
	mkdir -p build/host
	"$(HOST_CXX)" $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_bank_vm.cpp -o build/host/test_bank_vm
	./build/host/test_bank_vm

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# No threads: the bank is constant data.
test-thread:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread"

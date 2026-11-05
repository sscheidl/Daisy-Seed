# FV-1 interpreter FX component (research / feasibility study).
# Phase 1: header-only core, host tests and a host runner for cross-checks. The standalone FX firmware and the
# Toy Piano composition follow in phase 2 (not yet registered in projects.mk).
.DEFAULT_GOAL := test
HOST_CXX ?= g++
HOST_FLAGS = -std=c++17 -O2 -Wall -Wextra -Werror
HOST_INCLUDES = -Iinclude

.PHONY: test test-sanitize tool version
version:
	@sed -n 's/.*SEED_FV1VM_VERSION "\(.*\)".*/\1/p' include/fv1vm/fv1_vm.h

test:
	mkdir -p build/host
	"$(HOST_CXX)" $(HOST_FLAGS) $(HOST_INCLUDES) tests/test_fv1_vm.cpp -o build/host/test_fv1_vm
	./build/host/test_fv1_vm

test-sanitize:
	"$(MAKE)" test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# Host runner for cross-checks with an external reference; program files are never part of this repository.
tool:
	mkdir -p build/host
	"$(HOST_CXX)" $(HOST_FLAGS) $(HOST_INCLUDES) tools/fv1vm_run.cpp -o build/host/fv1vm_run

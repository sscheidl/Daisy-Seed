.DEFAULT_GOAL := all
# Variants (TEST_PLAN_PHASE2.md § 3.1), each in its own build directory:
#   make                     block 48, kMaxActive 8   -> build/ModBench-v<version>.bin
#   make BLOCK=256           block 256                -> build/b256-max8/
#   make MAX_ACTIVE=64       stress scenario S6       -> build/b48-max64/
VERSION := $(shell sed -n 's/.*MOD_BENCH_VERSION "\(.*\)".*/\1/p' src/version.h | head -n 1)
$(if $(VERSION),,$(error Could not read the version from src/version.h))
BLOCK ?= 48
MAX_ACTIVE ?= 8
TARGET = ModBench-v$(VERSION)
ifneq ($(BLOCK)$(MAX_ACTIVE),488)
override BUILD_DIR = build/b$(BLOCK)-max$(MAX_ACTIVE)
endif
CPP_SOURCES = src/main.cpp
C_DEFS = -DMOD_BENCH_BLOCK=$(BLOCK) -DMOD_ON_DAISY_MAX_ACTIVE=$(MAX_ACTIVE)
C_INCLUDES = -I../include -I../adapters -Isrc
# bench/ lies three levels below the repository; common/desktop.mk defaults to two.
DAISY_ROOT := ../../..
include ../../../common/desktop.mk
CPPFLAGS += -fstack-usage

.PHONY: test test-sanitize test-thread version
version:
	@echo $(VERSION)
# The module's host tests plus the scenario checks (run by `make PROJECT=mod-bench test`).
test test-sanitize test-thread:
	"$(MAKE)" -C .. $@

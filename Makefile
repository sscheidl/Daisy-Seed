.DEFAULT_GOAL := all
include projects.mk
PROJECT ?= toy-piano
.PHONY: components manifest test-components test-fx
components:
	python3 scripts/component_manifest.py --validate
manifest:
	python3 scripts/component_manifest.py --project $(PROJECT) --built
test-components:
	python3 -m unittest discover -s tests -p test_components.py -v
	python3 -m unittest discover -s tests -p test_firmware_checker.py -v
test-fx:
	$(MAKE) -C tests/fx test

PROJECT_ACTIONS := all clean libs test test-sanitize test-thread version program-dfu program-boot
.PHONY: help list all-projects test-projects test-release test-sd $(PROJECT_ACTIONS)

test-release:
	python3 -m unittest discover -s tests -p test_release_guard.py -v

help:
	@printf '%s\n' 'make [PROJECT=toy-piano] [all|test|test-sanitize|clean|libs]' 'make list                    Project directories and status' 'make all-projects            Build every implemented project' 'make test-projects           Test every implemented project' 'make PROJECT=<id> program-dfu  Explicit hardware flash (never automatic)'

list:
	@$(foreach p,$(PROJECTS),printf '%-20s %-12s %s\n' '$(p)' '$(PROJECT_STATUS_$(p))' '$(PROJECT_DIR_$(p))';)

$(PROJECT_ACTIONS):
	$(if $(PROJECT_DIR_$(PROJECT)),,$(error Unknown PROJECT '$(PROJECT)'; run make list))
	$(if $(filter $(PROJECT),$(BUILDABLE_PROJECTS)),,$(error PROJECT '$(PROJECT)' is planned and has no firmware yet))
	"$(MAKE)" -C $(PROJECT_DIR_$(PROJECT)) $(PROJECT_ARGS_$(PROJECT)) $@

all-projects:
	$(foreach p,$(BUILDABLE_PROJECTS),"$(MAKE)" PROJECT=$(p) all &&) true

test-projects: test-sd
	$(foreach p,$(BUILDABLE_PROJECTS),"$(MAKE)" PROJECT=$(p) test &&) true

# Shared read-only SD driver (common/sd): host tests on FAT32/exFAT images.
test-sd:
	"$(MAKE)" -C common/sd test

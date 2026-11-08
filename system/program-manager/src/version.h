#pragma once
// Daisy Program Manager: product version (file name, firmware marker, HELLO/PMINFO).
// Three-part SemVer, read by scripts/component_manifest.py and the image checks.
#define PROGRAM_MANAGER_VERSION "0.1.0"
// Embedded marker, found by tools/check_qspi_image.py in the BIN.
#define PROGRAM_MANAGER_MARKER "PROGRAM-MANAGER-FW-VERSION=" PROGRAM_MANAGER_VERSION

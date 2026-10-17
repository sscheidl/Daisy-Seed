#pragma once
// Single source of truth for the firmware version ("major.minor"). The Makefile
// (file name), CI and scripts/check-firmware.py read the quoted value on the
// definition below, so keep it on one line.
#define TOY_PIANO_VERSION "0.2"

namespace toy_piano {
constexpr char kVersion[] = TOY_PIANO_VERSION;
}

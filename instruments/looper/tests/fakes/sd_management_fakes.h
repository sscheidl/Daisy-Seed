// SPDX-License-Identifier: MIT
// Host fakes for the board-only SD management functions main.cpp calls
// (sd_management.cpp, fatfs_volume.cpp): backup SRAM, the personality and the card
// release before the restart. Include after main.cpp.
#pragma once
#include <cstdint>

static uint32_t intentWords[4];
static unsigned managementRuns = 0, cardReleases = 0;
volatile uint32_t* looper_intent_words() { return intentWords; }
void looper_run_sd_management(daisy::desktop_devkit::Hardware&) { ++managementRuns; }
namespace looper {
void ReleaseCard() { ++cardReleases; }
}  // namespace looper

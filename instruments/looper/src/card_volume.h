// SPDX-License-Identifier: MIT
// The microSD card: FatFS on the firmware (fatfs_volume.cpp), an in-memory
// card in host tests.
#pragma once
#include "loop_store.h"

namespace looper {

// Sets up SDMMC and FatFS once; the card is mounted on demand by Ready()
// while `present` reports a card in the slot.
bool InitCard(bool (*present)());
Volume& CardVolume();
// Before a restart into SD management: close and unmount, power the SD interface down.
void ReleaseCard();

}  // namespace looper

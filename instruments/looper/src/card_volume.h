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
// Before the restart into SD management: closes any open file, unmounts and resets
// the SD interface (SDMMC1 RCC reset), so the card reader starts from scratch.
void ReleaseCard();

}  // namespace looper

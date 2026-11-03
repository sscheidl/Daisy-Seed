// SPDX-License-Identifier: MIT
// Host stand-ins for common/sdshare/sd_management.cpp (board only) and the card release,
// for tests that compile the production main.cpp. Include once per test, after main.cpp.
#pragma once
#include "sd_management.h"
#include <stdexcept>

namespace sdshare {
inline volatile uint32_t intent_words[4];
inline unsigned restarts = 0, releases = 0, managements = 0;
inline BootIntent last_intent{};
volatile uint32_t* IntentWords() { return intent_words; }
void AbortSd() {}
void ReleaseSd() { ++releases; }
void RestartInto(uint32_t magic, const BootIntent& intent) {
    StoreBootIntent(magic, intent_words, intent);
    last_intent = intent;
    ++restarts;
}
void RunSdManagement(daisy::desktop_devkit::Hardware&, const ManagementConfig&) {
    ++managements;
    throw std::runtime_error("SD management");
}
}  // namespace sdshare
namespace looper {
inline unsigned card_releases = 0;
void ReleaseCard() { ++card_releases; }
}  // namespace looper

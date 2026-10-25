#pragma once
// SPDX-License-Identifier: MIT
#include <cstddef>
#define SEED_FX_CONTRACT_VERSION "1.0.0"

namespace seed::audio {
inline constexpr unsigned kFxContractMajor = 1, kFxContractMinor = 0;
struct Config {
    unsigned sample_rate = 48000;
    std::size_t max_block = 48;
};
struct StereoBlock {
    const float* left;
    const float* right;
    float* out_left;
    float* out_right;
    std::size_t frames;
};
// Exact per-channel in-place use is supported; partial overlap is not supported.
// Invalid blocks are rejected before touching any buffer. Zero frames is a no-op.
inline bool Valid(const Config& config, const StereoBlock& block) {
    return block.frames <= config.max_block &&
           (!block.frames || (block.left && block.right && block.out_left && block.out_right &&
                              block.out_left != block.out_right));
}
// Adapters implement Prepare(Config, Storage&), SetParameters(Parameters), and
// Process(StereoBlock). Storage and parameter types belong to each effect.
// Prepare is startup-only. Process is bounded, allocation/lock/I/O-free and wet-only.
// The host owns dry/wet mixing, routing, transitions and final output protection.
} // namespace seed::audio

#pragma once
// Version of the S1 load spike firmware (README.md). Not a product: no release, no manifest.
#define TEXTURE_SPIKE_VERSION "0.4.0"
#define TEXTURE_SPIKE_VERSION_MAJOR 0
#define TEXTURE_SPIKE_VERSION_MINOR 4
#define TEXTURE_SPIKE_VERSION_PATCH 0
// The string (firmware identity) and the numbers (CC identity reply) must agree; single digits.
static_assert(sizeof(TEXTURE_SPIKE_VERSION) == 6 && TEXTURE_SPIKE_VERSION[0] - '0' == TEXTURE_SPIKE_VERSION_MAJOR &&
                  TEXTURE_SPIKE_VERSION[2] - '0' == TEXTURE_SPIKE_VERSION_MINOR &&
                  TEXTURE_SPIKE_VERSION[4] - '0' == TEXTURE_SPIKE_VERSION_PATCH,
              "TEXTURE_SPIKE_VERSION and its numbers differ");

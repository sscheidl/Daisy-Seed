/* SHA-256 (FIPS 180-4), compact portable implementation without allocation.
 * Written for this repository (MIT, see /LICENSE); verified against the NIST
 * vectors in tests/test_sha256.cpp. A hash proves integrity of the bytes that
 * were hashed, never their origin. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t length; /* bytes processed so far */
    uint8_t block[64];
    size_t fill;
} pm_sha256;

void pm_sha256_init(pm_sha256* ctx);
void pm_sha256_update(pm_sha256* ctx, const void* data, size_t len);
void pm_sha256_final(pm_sha256* ctx, uint8_t digest[PM_SHA256_DIGEST_SIZE]);
/* One-shot helper. */
void pm_sha256_digest(const void* data, size_t len, uint8_t digest[PM_SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

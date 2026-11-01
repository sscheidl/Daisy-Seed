// SHA-256 against FIPS 180-4 / NIST CAVS vectors and the CRC-32 check value.
#include "check.h"
#include "pm_crc32.h"
#include "pm_sha256.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string Hex(const uint8_t* d, size_t n) {
    std::string s;
    char b[3];
    for(size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02x", d[i]);
        s += b;
    }
    return s;
}

std::string Digest(const std::string& msg) {
    uint8_t d[32];
    pm_sha256_digest(msg.data(), msg.size(), d);
    return Hex(d, 32);
}

} // namespace

int main() {
    // NIST FIPS 180-4 examples and well-known vectors.
    CHECK_EQ(Digest(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_EQ(Digest("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_EQ(Digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK_EQ(Digest("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
             "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    CHECK_EQ(Digest(std::string(1000000, 'a')),
             "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // Boundary lengths around the 64-byte block and the 56-byte padding threshold.
    CHECK_EQ(Digest(std::string(55, 'a')), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK_EQ(Digest(std::string(56, 'a')), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK_EQ(Digest(std::string(64, 'a')), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    CHECK_EQ(Digest(std::string(65, 'a')), "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");

    // Incremental updates in odd chunk sizes must equal the one-shot digest.
    std::string msg;
    for(int i = 0; i < 3000; ++i)
        msg.push_back(static_cast<char>(i * 31 + 7));
    pm_sha256 ctx;
    pm_sha256_init(&ctx);
    size_t pos = 0;
    size_t step = 1;
    while(pos < msg.size()) {
        const size_t n = std::min(step, msg.size() - pos);
        pm_sha256_update(&ctx, msg.data() + pos, n);
        pos += n;
        step = step * 3 % 97 + 1;
    }
    uint8_t inc[32];
    pm_sha256_final(&ctx, inc);
    CHECK_EQ(Hex(inc, 32), Digest(msg));

    // The context is wiped after final.
    bool zero = true;
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&ctx);
    for(size_t i = 0; i < sizeof(ctx); ++i)
        zero = zero && raw[i] == 0;
    CHECK(zero);

    // CRC-32 check value and incremental equivalence.
    const char* check = "123456789";
    CHECK_EQ(pm::Crc32(reinterpret_cast<const uint8_t*>(check), 9), 0xCBF43926u);
    CHECK_EQ(pm::Crc32(nullptr, 0), 0u);
    uint32_t crc = 0xFFFFFFFFu;
    crc = pm::Crc32Update(crc, reinterpret_cast<const uint8_t*>(check), 4);
    crc = pm::Crc32Update(crc, reinterpret_cast<const uint8_t*>(check) + 4, 5);
    CHECK_EQ(crc ^ 0xFFFFFFFFu, 0xCBF43926u);
    return check::Finish("test_sha256");
}

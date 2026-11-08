// .dsy header parsing: every field is checked, every rejection reason reachable.
#include "check.h"
#include "pm_crc32.h"
#include "pm_image.h"
#include <cstring>
#include <vector>

using namespace pm;

namespace {

ImageHeader Good() {
    ImageHeader h;
    h.header_version = kHeaderVersion;
    std::strcpy(h.hardware, kHardwareId);
    std::strcpy(h.profile, kProfileId);
    std::strcpy(h.product, "toy-piano");
    std::strcpy(h.version, "0.4.0");
    h.load_address = kTargetBase;
    h.payload_offset = kHeaderSize;
    h.payload_length = 123456;
    for(int i = 0; i < 32; ++i)
        h.payload_sha256[i] = static_cast<uint8_t>(i * 7);
    return h;
}

void Fix(uint8_t* bytes) {  // recompute the CRC after a deliberate field change
    const uint32_t crc = Crc32(bytes, 156);
    bytes[156] = static_cast<uint8_t>(crc); bytes[157] = static_cast<uint8_t>(crc >> 8);
    bytes[158] = static_cast<uint8_t>(crc >> 16); bytes[159] = static_cast<uint8_t>(crc >> 24);
}

ImageError ParseBytes(const uint8_t* bytes, size_t len = kHeaderSize) {
    ImageHeader out;
    return ParseHeader(bytes, len, out);
}

} // namespace

int main() {
    uint8_t bytes[kHeaderSize];
    BuildHeader(Good(), bytes);
    ImageHeader parsed;
    CHECK(ParseHeader(bytes, sizeof(bytes), parsed) == ImageError::None);
    CHECK_EQ(parsed.header_version, 1);
    CHECK_STREQ(parsed.hardware, "seed3-desktop");
    CHECK_STREQ(parsed.profile, "axi-sram-480k-v1");
    CHECK_STREQ(parsed.product, "toy-piano");
    CHECK_STREQ(parsed.version, "0.4.0");
    CHECK_EQ(parsed.load_address, 0x24000000u);
    CHECK_EQ(parsed.payload_offset, 160u);
    CHECK_EQ(parsed.payload_length, 123456u);
    CHECK_EQ(parsed.payload_sha256[5], 35);
    // Fixed layout positions (wire format contract).
    CHECK(std::memcmp(bytes, "DSYIMAGE", 8) == 0);
    CHECK_EQ(bytes[8], 1); CHECK_EQ(bytes[9], 0);
    CHECK_EQ(bytes[10], 160); CHECK_EQ(bytes[11], 0);
    CHECK_EQ(bytes[104], 0x00); CHECK_EQ(bytes[107], 0x24);
    CHECK_EQ(bytes[112], 0x40); CHECK_EQ(bytes[113], 0xE2); CHECK_EQ(bytes[114], 0x01);

    // Rejections, in the order the parser checks them.
    CHECK(ParseBytes(bytes, 159) == ImageError::Short);
    CHECK(ParseBytes(nullptr) == ImageError::Short);
    std::vector<uint8_t> v(bytes, bytes + kHeaderSize);
    v[0] = 'X'; CHECK(ParseBytes(v.data()) == ImageError::Magic);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[120] ^= 1; CHECK(ParseBytes(v.data()) == ImageError::Crc);   // any bit flip -> CRC
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[159] ^= 1; CHECK(ParseBytes(v.data()) == ImageError::Crc);   // damaged CRC itself
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[8] = 2; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::Version);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[10] = 128; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::HeaderSize);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[12] = 1; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::Flags);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[16] = 'S'; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::Hardware);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[31] = 'x'; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::Hardware);  // no terminator
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[30] = 'x'; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::Hardware);  // junk after NUL
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[32] = 'b'; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::Profile);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[56] = 'T'; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::Product);   // uppercase
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[56] = 0; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::Product);     // empty
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[88] = ' '; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::ProductVersion);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[107] = 0x20; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::LoadAddress);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[108] = 0; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::PayloadOffset);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[112] = 7; v[113] = 0; v[114] = 0; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::PayloadLength);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[112] = 0x01; v[113] = 0x80; v[114] = 0x07; Fix(v.data());  // 0x078001 = 480 KiB + 1
    CHECK(ParseBytes(v.data()) == ImageError::PayloadLength);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[112] = 0x00; v[113] = 0x80; v[114] = 0x07; Fix(v.data());  // exactly 480 KiB is allowed
    CHECK(ParseBytes(v.data()) == ImageError::None);
    v = std::vector<uint8_t>(bytes, bytes + kHeaderSize);
    v[150] = 1; Fix(v.data()); CHECK(ParseBytes(v.data()) == ImageError::Reserved);

    // Vector checks on the loaded payload.
    uint8_t vec[8] = {0x00, 0x00, 0x02, 0x20, 0x01, 0x01, 0x00, 0x24};  // SP 0x20020000, PC 0x24000101
    CHECK(CheckVectors(vec, 0x200) == VectorError::None);
    CHECK(CheckVectors(vec, 0x100) == VectorError::Reset);   // entry beyond the payload
    vec[4] = 0x00; CHECK(CheckVectors(vec, 0x200) == VectorError::Reset);  // ARM, not Thumb
    vec[4] = 0x01; vec[7] = 0x90; CHECK(CheckVectors(vec, 0x200) == VectorError::Reset);  // QSPI entry
    vec[7] = 0x24; vec[3] = 0x24; CHECK(CheckVectors(vec, 0x200) == VectorError::Stack);  // SP in AXI
    vec[3] = 0x20; vec[2] = 0x00; vec[1] = 0x00; vec[0] = 0x00;  // SP == DTCM base: not allowed
    CHECK(CheckVectors(vec, 0x200) == VectorError::Stack);
    vec[0] = 0x04; CHECK(CheckVectors(vec, 0x200) == VectorError::Stack);  // not 8-byte aligned
    vec[0] = 0x08; CHECK(CheckVectors(vec, 0x200) == VectorError::None);

    CHECK_STREQ(ImageErrorName(ImageError::Crc), "HDRCRC");
    CHECK_STREQ(VectorErrorName(VectorError::Stack), "VECSP");
    return check::Finish("test_image");
}

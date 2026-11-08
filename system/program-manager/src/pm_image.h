// .dsy package header (format 1) and the BOOT_SRAM image profile "axi-sram-480k-v1".
// Portable: no libDaisy, no allocation. The wire format is documented in
// docs/PACKAGE.md; tools/dsy_pack.py writes it, this code reads it on the device.
//
// Layout, little-endian, 160 bytes:
//   0   char[8]  magic           "DSYIMAGE"
//   8   u16      header_version  1
//   10  u16      header_size     160
//   12  u32      flags           0 (any set bit is rejected: unknown feature)
//   16  char[16] hardware        "seed3-desktop", NUL padded
//   32  char[24] profile         "axi-sram-480k-v1", NUL padded
//   56  char[32] product         product id, e.g. "toy-piano"
//   88  char[16] version         product version, e.g. "0.4.0"
//   104 u32      load_address    0x24000000
//   108 u32      payload_offset  160
//   112 u32      payload_length  8 .. 491520
//   116 u8[32]   payload_sha256  SHA-256 over exactly payload_length payload bytes
//   148 u32[2]   reserved        0
//   156 u32      header_crc32    CRC-32 over bytes 0..155
//   160 payload (one contiguous BOOT_SRAM image for 0x24000000)
#pragma once
#include <cstddef>
#include <cstdint>

namespace pm {

constexpr uint32_t kHeaderSize = 160;
constexpr uint16_t kHeaderVersion = 1;
constexpr uint32_t kTargetBase = 0x24000000u;      // AXI SRAM image target
constexpr uint32_t kTargetSize = 480u * 1024u;      // exclusive end 0x24078000
constexpr uint32_t kMinPayload = 8;                 // at least the first two vectors
constexpr uint32_t kDtcmBase = 0x20000000u;
constexpr uint32_t kDtcmEnd = 0x20020000u;          // _estack of the official linkers
constexpr char kMagic[8] = {'D', 'S', 'Y', 'I', 'M', 'A', 'G', 'E'};
constexpr const char* kHardwareId = "seed3-desktop";
constexpr const char* kProfileId = "axi-sram-480k-v1";

constexpr size_t kHardwareField = 16, kProfileField = 24, kProductField = 32, kVersionField = 16;

struct ImageHeader {
    uint16_t header_version = 0;
    uint32_t flags = 0;
    char hardware[kHardwareField + 1] = {};
    char profile[kProfileField + 1] = {};
    char product[kProductField + 1] = {};
    char version[kVersionField + 1] = {};
    uint32_t load_address = 0;
    uint32_t payload_offset = 0;
    uint32_t payload_length = 0;
    uint8_t payload_sha256[32] = {};
};

enum class ImageError : uint8_t {
    None = 0,
    Short,          // fewer than kHeaderSize bytes available
    Magic,
    Version,        // header_version != 1
    HeaderSize,     // header_size != 160
    Crc,
    Flags,          // unknown flag bits
    Hardware,       // not "seed3-desktop" (or malformed string field)
    Profile,        // not "axi-sram-480k-v1"
    Product,        // empty, too long or not [a-z0-9-]
    ProductVersion, // empty, too long or not [0-9A-Za-z.-]
    LoadAddress,    // != 0x24000000
    PayloadOffset,  // != header_size
    PayloadLength,  // outside kMinPayload..kTargetSize
    Reserved,       // reserved words not zero
};

const char* ImageErrorName(ImageError error);

// Parses and validates a header from `len` bytes (needs kHeaderSize). On success
// the string fields in `out` are NUL-terminated copies.
ImageError ParseHeader(const uint8_t* bytes, size_t len, ImageHeader& out);

// Serializes `header` (CRC computed here) into out[kHeaderSize]. For host tools and tests.
void BuildHeader(const ImageHeader& header, uint8_t out[kHeaderSize]);

enum class VectorError : uint8_t { None = 0, Stack, Reset };
const char* VectorErrorName(VectorError error);
// Checks the first two words of a loaded payload: initial MSP inside DTCM
// (0x20000000 < sp <= 0x20020000) and a Thumb reset vector inside the payload.
VectorError CheckVectors(const uint8_t payload[8], uint32_t payload_length);

// Character classes shared with the catalog parser.
bool IsProductIdChar(char c);   // [a-z0-9-]
bool IsVersionChar(char c);     // [0-9A-Za-z.-]

} // namespace pm

#include "pm_image.h"
#include "pm_crc32.h"
#include <cstring>

namespace pm {
namespace {

uint16_t Load16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Load32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
           | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void Store16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
void Store32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16); p[3] = static_cast<uint8_t>(v >> 24);
}

// A fixed field holds a non-empty string, NUL terminated inside the field, with only
// zero bytes after the terminator and only characters accepted by `ok`.
bool CopyField(const uint8_t* field, size_t size, char* out, bool (*ok)(char)) {
    size_t n = 0;
    while(n < size && field[n] != 0) {
        if(!ok(static_cast<char>(field[n])))
            return false;
        out[n] = static_cast<char>(field[n]);
        ++n;
    }
    if(n == 0 || n == size)  // empty, or no terminator inside the field
        return false;
    out[n] = 0;
    for(size_t i = n; i < size; ++i)
        if(field[i] != 0)
            return false;
    return true;
}

bool IsIdOrVersionChar(char c) { return IsProductIdChar(c) || IsVersionChar(c); }

// Copies at most `size` bytes of a string into a zeroed fixed field (no terminator needed).
void PutField(uint8_t* field, size_t size, const char* text) {
    size_t n = 0;
    while(n < size && text[n] != 0)
        ++n;
    std::memcpy(field, text, n);
}

} // namespace

bool IsProductIdChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

bool IsVersionChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.'
           || c == '-';
}

const char* ImageErrorName(ImageError error) {
    switch(error) {
        case ImageError::None: return "OK";
        case ImageError::Short: return "SHORT";
        case ImageError::Magic: return "MAGIC";
        case ImageError::Version: return "HDRVER";
        case ImageError::HeaderSize: return "HDRSIZE";
        case ImageError::Crc: return "HDRCRC";
        case ImageError::Flags: return "FLAGS";
        case ImageError::Hardware: return "HARDWARE";
        case ImageError::Profile: return "PROFILE";
        case ImageError::Product: return "PRODUCT";
        case ImageError::ProductVersion: return "VERSION";
        case ImageError::LoadAddress: return "LOADADDR";
        case ImageError::PayloadOffset: return "OFFSET";
        case ImageError::PayloadLength: return "LENGTH";
        case ImageError::Reserved: return "RESERVED";
    }
    return "?";
}

const char* VectorErrorName(VectorError error) {
    switch(error) {
        case VectorError::None: return "OK";
        case VectorError::Stack: return "VECSP";
        case VectorError::Reset: return "VECPC";
    }
    return "?";
}

ImageError ParseHeader(const uint8_t* bytes, size_t len, ImageHeader& out) {
    out = ImageHeader();
    if(bytes == nullptr || len < kHeaderSize)
        return ImageError::Short;
    if(std::memcmp(bytes, kMagic, sizeof(kMagic)) != 0)
        return ImageError::Magic;
    // The CRC is checked before any field is interpreted, so a damaged header is
    // reported as such rather than as a wrong value.
    if(Load32(bytes + 156) != Crc32(bytes, 156))
        return ImageError::Crc;
    out.header_version = Load16(bytes + 8);
    if(out.header_version != kHeaderVersion)
        return ImageError::Version;
    if(Load16(bytes + 10) != kHeaderSize)
        return ImageError::HeaderSize;
    out.flags = Load32(bytes + 12);
    if(out.flags != 0)
        return ImageError::Flags;
    if(!CopyField(bytes + 16, kHardwareField, out.hardware, IsIdOrVersionChar)
       || std::strcmp(out.hardware, kHardwareId) != 0)
        return ImageError::Hardware;
    if(!CopyField(bytes + 32, kProfileField, out.profile, IsIdOrVersionChar)
       || std::strcmp(out.profile, kProfileId) != 0)
        return ImageError::Profile;
    if(!CopyField(bytes + 56, kProductField, out.product, IsProductIdChar))
        return ImageError::Product;
    if(!CopyField(bytes + 88, kVersionField, out.version, IsVersionChar))
        return ImageError::ProductVersion;
    out.load_address = Load32(bytes + 104);
    if(out.load_address != kTargetBase)
        return ImageError::LoadAddress;
    out.payload_offset = Load32(bytes + 108);
    if(out.payload_offset != kHeaderSize)
        return ImageError::PayloadOffset;
    out.payload_length = Load32(bytes + 112);
    if(out.payload_length < kMinPayload || out.payload_length > kTargetSize)
        return ImageError::PayloadLength;
    std::memcpy(out.payload_sha256, bytes + 116, 32);
    if(Load32(bytes + 148) != 0 || Load32(bytes + 152) != 0)
        return ImageError::Reserved;
    return ImageError::None;
}

void BuildHeader(const ImageHeader& header, uint8_t out[kHeaderSize]) {
    std::memset(out, 0, kHeaderSize);
    std::memcpy(out, kMagic, sizeof(kMagic));
    Store16(out + 8, header.header_version);
    Store16(out + 10, static_cast<uint16_t>(kHeaderSize));
    Store32(out + 12, header.flags);
    PutField(out + 16, kHardwareField, header.hardware);
    PutField(out + 32, kProfileField, header.profile);
    PutField(out + 56, kProductField, header.product);
    PutField(out + 88, kVersionField, header.version);
    Store32(out + 104, header.load_address);
    Store32(out + 108, header.payload_offset);
    Store32(out + 112, header.payload_length);
    std::memcpy(out + 116, header.payload_sha256, 32);
    Store32(out + 156, Crc32(out, 156));
}

VectorError CheckVectors(const uint8_t payload[8], uint32_t payload_length) {
    const uint32_t sp = Load32(payload);
    const uint32_t pc = Load32(payload + 4);
    if(!(sp > kDtcmBase && sp <= kDtcmEnd) || (sp & 7u) != 0)
        return VectorError::Stack;
    if((pc & 1u) == 0)
        return VectorError::Reset;
    const uint32_t entry = pc & ~1u;
    if(entry < kTargetBase || entry >= kTargetBase + payload_length)
        return VectorError::Reset;
    return VectorError::None;
}

} // namespace pm

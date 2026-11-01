// Host fakes for the loader and manager: an in-memory card, a target buffer and a
// recording boot commit. Fault injection by byte position and call count.
#pragma once
#include "pm_loader.h"
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace fakes {

struct FakeFile {
    std::vector<uint8_t> bytes;
};

class FakeSource final : public pm::ImageSource {
  public:
    std::map<std::string, FakeFile> files;
    bool card_present = true;
    uint32_t max_read = 0xFFFFFFFFu;  // partial reads: at most this many bytes per Read()
    int64_t fail_read_at = -1;        // Read() returns -1 once this byte offset is reached
    int64_t remove_card_at = -1;      // card_present becomes false once this offset is read
    int64_t truncate_at = -1;         // the file ends early at this offset (EOF)
    uint32_t reported_size_delta = 0; // Open() reports size + delta (size lie)
    int opens = 0, closes = 0, reads = 0;
    bool is_open = false;

    bool Open(const char* path, uint32_t& file_size) override {
        ++opens;
        auto it = files.find(path);
        if(it == files.end())
            return false;
        current_ = &it->second;
        pos_ = 0;
        is_open = true;
        file_size = static_cast<uint32_t>(current_->bytes.size()) + reported_size_delta;
        return true;
    }
    int32_t Read(uint8_t* dst, uint32_t len) override {
        ++reads;
        if(!is_open)
            return -1;
        if(fail_read_at >= 0 && static_cast<int64_t>(pos_) >= fail_read_at)
            return -1;
        size_t end = current_->bytes.size();
        if(truncate_at >= 0 && static_cast<size_t>(truncate_at) < end)
            end = static_cast<size_t>(truncate_at);
        if(pos_ >= end)
            return 0;
        size_t n = end - pos_;
        if(n > len) n = len;
        if(n > max_read) n = max_read;
        if(fail_read_at >= 0 && static_cast<int64_t>(pos_ + n) > fail_read_at)
            n = static_cast<size_t>(fail_read_at) - pos_;  // deliver up to the fault, fail next
        std::memcpy(dst, current_->bytes.data() + pos_, n);
        pos_ += n;
        if(remove_card_at >= 0 && static_cast<int64_t>(pos_) >= remove_card_at)
            card_present = false;
        return static_cast<int32_t>(n);
    }
    void Close() override {
        ++closes;
        is_open = false;
    }
    bool CardPresent() override { return card_present; }

  private:
    FakeFile* current_ = nullptr;
    size_t pos_ = 0;
};

class FakeTarget final : public pm::LoadTarget {
  public:
    explicit FakeTarget(uint32_t capacity = pm::kTargetSize) : buffer_(capacity, 0xA5) {}
    uint8_t* Base() override { return buffer_.data(); }
    uint32_t Capacity() override { return static_cast<uint32_t>(buffer_.size()); }
    void Clean(uint32_t len) override {
        ++cleans;
        cleaned_len = len;
        log->push_back("clean");
    }
    std::vector<uint8_t> buffer_;
    int cleans = 0;
    uint32_t cleaned_len = 0;
    std::vector<std::string>* log = nullptr;
};

class FakeCommit final : public pm::BootCommit {
  public:
    void Quiesce() override {
        ++quiesces;
        log->push_back("quiesce");
    }
    void Commit(uint32_t address) override {
        ++commits;
        committed_address = address;
        log->push_back("commit");
    }
    int quiesces = 0, commits = 0;
    uint32_t committed_address = 0;
    std::vector<std::string>* log = nullptr;
};

// Builds a complete .dsy file: header + payload with valid vectors and the real hash.
inline std::vector<uint8_t> MakePackage(const char* product, const char* version, uint32_t payload_len,
                                        pm::ImageHeader* header_out = nullptr) {
    std::vector<uint8_t> payload(payload_len);
    for(uint32_t i = 0; i < payload_len; ++i)
        payload[i] = static_cast<uint8_t>(i * 13 + 5);
    // Vector table: SP 0x20020000, reset vector 0x24000101 (Thumb, inside the image).
    const uint8_t vectors[8] = {0x00, 0x00, 0x02, 0x20, 0x01, 0x01, 0x00, 0x24};
    std::memcpy(payload.data(), vectors, 8);
    pm::ImageHeader h;
    h.header_version = pm::kHeaderVersion;
    std::strcpy(h.hardware, pm::kHardwareId);
    std::strcpy(h.profile, pm::kProfileId);
    std::strncpy(h.product, product, pm::kProductField);
    std::strncpy(h.version, version, pm::kVersionField);
    h.load_address = pm::kTargetBase;
    h.payload_offset = pm::kHeaderSize;
    h.payload_length = payload_len;
    pm_sha256_digest(payload.data(), payload.size(), h.payload_sha256);
    std::vector<uint8_t> file(pm::kHeaderSize);
    pm::BuildHeader(h, file.data());
    file.insert(file.end(), payload.begin(), payload.end());
    if(header_out)
        *header_out = h;
    return file;
}

inline pm::CatalogEntry MakeEntry(uint8_t slot, const char* id, const char* path, const pm::ImageHeader& h) {
    pm::CatalogEntry e;
    e.slot = slot;
    std::strncpy(e.id, id, pm::kIdMax);
    std::strncpy(e.name, id, pm::kNameMax);
    std::strcpy(e.category, "other");
    std::strncpy(e.version, h.version, pm::kVersionMax);
    std::strncpy(e.path, path, pm::kPathMax);
    std::memcpy(e.payload_sha256, h.payload_sha256, 32);
    return e;
}

} // namespace fakes

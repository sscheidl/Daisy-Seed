// Minimal diag::Board / diag::UsbSink fakes and a frame collector for protocol tests.
#pragma once
#include "diag_app.h"
#include <string>
#include <vector>

namespace fakes {

class FakeUsb final : public diag::UsbSink {
  public:
    std::string delivered;
    diag::SendResult Send(const uint8_t* data, size_t len) override {
        delivered.append(reinterpret_cast<const char*>(data), len);
        return diag::SendResult::Ok;
    }
    // Returns the frame bodies received since the last call (CRC trailer stripped).
    std::vector<std::string> TakeBodies() {
        std::vector<std::string> out;
        size_t pos = 0;
        while(true) {
            const size_t start = delivered.find('$', pos);
            if(start == std::string::npos)
                break;
            const size_t end = delivered.find('\n', start);
            if(end == std::string::npos)
                break;
            const size_t star = delivered.rfind('*', end);
            out.push_back(delivered.substr(start + 1, star - start - 1));
            pos = end + 1;
        }
        delivered.erase(0, pos);
        return out;
    }
};

class FakeBoard final : public diag::Board {
  public:
    diag::Inputs inputs;
    uint32_t ticks = 0;
    std::vector<std::pair<uint16_t, uint8_t>> led_writes;
    std::vector<std::pair<uint16_t, uint16_t>> cv_writes;
    int sd_tests = 0;
    int boot_entries = 0;

    diag::Inputs ReadInputs() override { return inputs; }
    uint32_t ControlTicks() override { return ticks; }
    void ApplyLeds(uint16_t leds, uint8_t br) override { led_writes.emplace_back(leds, br); }
    void ApplyCvOut(uint16_t c1, uint16_t c2) override { cv_writes.emplace_back(c1, c2); }
    diag::SdResult RunSdTest(bool) override {
        ++sd_tests;
        return diag::SdResult();
    }
    void EnterBootloader() override { ++boot_entries; }
};

inline std::string Frame(const std::string& body) {
    char frame[ddp::kMaxFrame];
    const size_t n = ddp::BuildFrame(body.data(), body.size(), frame, sizeof(frame));
    return std::string(frame, n);
}

// "key=value" lookup in a reply body; empty string when absent.
inline std::string Field(const std::string& body, const std::string& key) {
    const std::string needle = " " + key + "=";
    const size_t p = body.find(needle);
    if(p == std::string::npos)
        return "";
    const size_t start = p + needle.size();
    const size_t end = body.find(' ', start);
    return body.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

inline bool Contains(const std::vector<std::string>& bodies, const std::string& prefix) {
    for(const auto& b : bodies)
        if(b.compare(0, prefix.size(), prefix) == 0)
            return true;
    return false;
}

inline std::string First(const std::vector<std::string>& bodies, const std::string& prefix) {
    for(const auto& b : bodies)
        if(b.compare(0, prefix.size(), prefix) == 0)
            return b;
    return "";
}

} // namespace fakes

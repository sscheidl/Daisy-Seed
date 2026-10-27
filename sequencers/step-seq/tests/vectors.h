#pragma once
// Minimal readers for the shared contract files (contracts/*.json) in C++ tests. They
// rely on the fixed layout written by tools/sq_codec.py and the transport vector writer.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace vectors {

inline std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if(!in) {
        std::printf("FAIL: cannot read %s\n", path.c_str());
        std::exit(1);
    }
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

// Value of "key": in block, as raw text (string without quotes, number or array).
inline bool Field(const std::string& block, const std::string& key, std::string& out) {
    const std::string tag = "\"" + key + "\":";
    std::size_t p = block.find(tag);
    if(p == std::string::npos)
        return false;
    p += tag.size();
    while(p < block.size() && block[p] == ' ')
        ++p;
    if(block[p] == '"') {
        const std::size_t end = block.find('"', p + 1);
        out = block.substr(p + 1, end - p - 1);
    } else if(block[p] == '[') {
        int depth = 0;
        std::size_t end = p;
        for(; end < block.size(); ++end) {
            if(block[end] == '[')
                ++depth;
            if(block[end] == ']' && --depth == 0)
                break;
        }
        out = block.substr(p + 1, end - p - 1);
    } else {
        std::size_t end = p;
        while(end < block.size() && block[end] != ',' && block[end] != '\n' && block[end] != '}')
            ++end;
        out = block.substr(p, end - p);
    }
    return true;
}

inline std::vector<uint8_t> Hex(const std::string& text) {
    std::vector<uint8_t> bytes;
    std::istringstream in(text);
    std::string part;
    while(in >> part)
        bytes.push_back(uint8_t(std::strtoul(part.c_str(), nullptr, 16)));
    return bytes;
}

inline std::vector<long> Numbers(const std::string& text) {
    std::vector<long> values;
    const char* p = text.c_str();
    while(*p) {
        if((*p >= '0' && *p <= '9') || *p == '-') {
            char* end = nullptr;
            values.push_back(std::strtol(p, &end, 10));
            p = end;
        } else {
            ++p;
        }
    }
    return values;
}

// Splits the file into blocks that each start at one occurrence of "key":.
inline std::vector<std::string> Blocks(const std::string& text, const std::string& key) {
    std::vector<std::string> blocks;
    const std::string tag = "\"" + key + "\":";
    std::size_t p = text.find(tag);
    while(p != std::string::npos) {
        const std::size_t next = text.find(tag, p + tag.size());
        blocks.push_back(text.substr(p, next == std::string::npos ? std::string::npos : next - p));
        p = next;
    }
    return blocks;
}

} // namespace vectors

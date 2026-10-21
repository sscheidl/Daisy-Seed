// SPDX-License-Identifier: MIT
// In-memory card for host tests, with failure injection. Rename fails when the
// target exists, as FatFS f_rename does.
#pragma once
#include "loop_store.h"
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

struct MemoryVolume : looper::Volume {
    std::map<std::string, std::vector<uint8_t>> files;
    std::set<std::string> directories;
    bool ready = true;
    long failWriteAfter = -1;  // bytes written by this volume before writes fail
    long written = 0;
    bool failRename = false;
    std::string open;
    bool writing = false;
    uint32_t outOfRangeReads = 0;

    bool Ready() override { return ready; }
    bool OpenRead(const char* path) override {
        if (!ready || !open.empty() || !files.count(path))
            return false;
        open = path;
        writing = false;
        return true;
    }
    bool OpenWrite(const char* path) override {
        if (!ready || !open.empty())
            return false;
        files[path].clear();
        open = path;
        writing = true;
        return true;
    }
    uint32_t Size() override { return open.empty() ? 0 : uint32_t(files[open].size()); }
    bool ReadAt(uint32_t offset, void* destination, uint32_t bytes) override {
        if (!ready || open.empty())
            return false;
        const auto& f = files[open];
        if (uint64_t(offset) + bytes > f.size()) {
            ++outOfRangeReads;
            return false;
        }
        std::memcpy(destination, f.data() + offset, bytes);
        return true;
    }
    bool Write(const void* source, uint32_t bytes) override {
        if (!ready || open.empty() || !writing)
            return false;
        if (failWriteAfter >= 0 && written + long(bytes) > failWriteAfter)
            return false;
        auto& f = files[open];
        const auto* p = static_cast<const uint8_t*>(source);
        f.insert(f.end(), p, p + bytes);
        written += bytes;
        return true;
    }
    bool Close() override {
        const bool ok = ready && !open.empty();
        open.clear();
        return ok;
    }
    bool Remove(const char* path) override {
        if (!ready)
            return false;
        files.erase(path);
        return true;
    }
    bool Rename(const char* from, const char* to) override {
        if (!ready || failRename || !files.count(from) || files.count(to))
            return false;
        files[to] = std::move(files[from]);
        files.erase(from);
        return true;
    }
    bool MakeDirectory(const char* path) override {
        if (!ready)
            return false;
        directories.insert(path);
        return true;
    }
    bool List(const char* path, Visit visit, void* context) override {
        if (!ready)
            return false;
        const std::string prefix = std::string(path) + "/";
        for (const auto& [name, data] : files) {
            if (name.compare(0, prefix.size(), prefix) != 0)
                continue;
            const std::string rest = name.substr(prefix.size());
            if (rest.find('/') == std::string::npos)
                visit(context, rest.c_str());
        }
        return true;
    }
};

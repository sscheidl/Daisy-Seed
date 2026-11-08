// Fake card for the manager: catalog text, root listing and the FakeSource files.
#pragma once
#include "fake_platform.h"
#include "pm_manager.h"

namespace fakes {

class FakeStorage final : public pm::Storage {
  public:
    std::string catalog;              // /DAISY/catalog.json text
    bool catalog_missing = false;
    bool mount_fails = false;
    bool read_fails = false;
    bool root_bin = false;
    int mounts = 0, unmounts = 0, catalog_reads = 0;
    FakeSource source;                // program files

    bool Open(const char* path, uint32_t& file_size) override { return source.Open(path, file_size); }
    int32_t Read(uint8_t* dst, uint32_t len) override { return source.Read(dst, len); }
    void Close() override { source.Close(); }
    bool CardPresent() override { return source.card_present; }

    CatalogResult ReadCatalog(char* buf, uint32_t cap, uint32_t& len, int& detail) override {
        ++catalog_reads;
        detail = 0;
        if(!source.card_present)
            return CatalogResult::NoCard;
        if(mount_fails) {
            detail = 13;  // FR_NO_FILESYSTEM
            return CatalogResult::MountFailed;
        }
        ++mounts;
        if(catalog_missing) {
            detail = 4;  // FR_NO_FILE
            return CatalogResult::Missing;
        }
        if(read_fails) {
            detail = 1;  // FR_DISK_ERR
            return CatalogResult::ReadError;
        }
        if(catalog.size() > cap)
            return CatalogResult::TooLarge;
        std::memcpy(buf, catalog.data(), catalog.size());
        len = static_cast<uint32_t>(catalog.size());
        return CatalogResult::Ok;
    }
    bool RootHasBinFile() override { return root_bin; }
    void Unmount() override { ++unmounts; }
};

} // namespace fakes

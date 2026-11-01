#include "pm_loader.h"
#include <cstring>

namespace pm {

const char* LoadStateName(LoadState state) {
    switch(state) {
        case LoadState::Idle: return "IDLE";
        case LoadState::Opening: return "OPENING";
        case LoadState::Header: return "HEADER";
        case LoadState::Payload: return "LOADING";
        case LoadState::Verify: return "VERIFY";
        case LoadState::Finalize: return "FINALIZE";
        case LoadState::Committed: return "RESTARTING";
        case LoadState::Failed: return "ERROR";
    }
    return "?";
}

const char* LoadErrorName(LoadError error) {
    switch(error) {
        case LoadError::None: return "OK";
        case LoadError::Busy: return "BUSY";
        case LoadError::NoEntry: return "NOENTRY";
        case LoadError::CardAbsent: return "NOCARD";
        case LoadError::Open: return "OPEN";
        case LoadError::FileSize: return "FILESIZE";
        case LoadError::HeaderRead: return "HDRREAD";
        case LoadError::Header: return "HEADER";
        case LoadError::Product: return "PRODUCT";
        case LoadError::HashMismatch: return "CATHASH";
        case LoadError::Read: return "READ";
        case LoadError::Short: return "SHORT";
        case LoadError::Long: return "LONG";
        case LoadError::Hash: return "HASH";
        case LoadError::Vector: return "VECTOR";
        case LoadError::Budget: return "BUDGET";
        case LoadError::Cancelled: return "CANCELLED";
    }
    return "?";
}

Loader::Loader(ImageSource& source, LoadTarget& target, BootCommit& commit)
    : source_(source), target_(target), commit_(commit) {
    std::memset(&sha_, 0, sizeof(sha_));
    std::memset(header_bytes_, 0, sizeof(header_bytes_));
}

bool Loader::Active() const {
    switch(status_.state) {
        case LoadState::Opening:
        case LoadState::Header:
        case LoadState::Payload:
        case LoadState::Verify:
        case LoadState::Finalize: return true;
        default: return false;
    }
}

uint8_t Loader::Percent() const {
    switch(status_.state) {
        case LoadState::Idle:
        case LoadState::Opening:
        case LoadState::Header: return 0;
        case LoadState::Payload:
            return status_.bytes_total
                       ? static_cast<uint8_t>(static_cast<uint64_t>(status_.bytes_done) * 99u / status_.bytes_total)
                       : 0;
        case LoadState::Verify:
        case LoadState::Finalize: return 99;
        case LoadState::Committed: return 100;
        case LoadState::Failed:
            return status_.bytes_total
                       ? static_cast<uint8_t>(static_cast<uint64_t>(status_.bytes_done) * 99u / status_.bytes_total)
                       : 0;
    }
    return 0;
}

bool Loader::Start(const CatalogEntry& entry, uint32_t now_ms, uint32_t request_id) {
    if(Active()) {
        status_.error = LoadError::Busy;
        return false;
    }
    status_ = LoadStatus();
    status_.state = LoadState::Opening;
    status_.slot = entry.slot;
    status_.request_id = request_id;
    status_.started_ms = now_ms;
    entry_ = entry;
    header_ = ImageHeader();
    file_size_ = 0;
    return true;
}

void Loader::Fail(LoadError error) {
    if(open_) {
        source_.Close();
        open_ = false;
    }
    status_.state = LoadState::Failed;
    status_.error = error;
}

void Loader::Cancel() {
    if(Active())
        Fail(LoadError::Cancelled);
}

void Loader::Reset() {
    if(Active())
        Fail(LoadError::Cancelled);
    status_ = LoadStatus();
}

bool Loader::ReadExact(uint8_t* dst, uint32_t len) {
    uint32_t got = 0;
    while(got < len) {
        const int32_t n = source_.Read(dst + got, len - got);
        if(n <= 0)
            return false;
        got += static_cast<uint32_t>(n);
    }
    return true;
}

bool Loader::Step(uint32_t now_ms) {
    if(!Active())
        return false;
    if(now_ms - status_.started_ms > kBudgetMs) {
        Fail(LoadError::Budget);
        return false;
    }
    if(status_.state != LoadState::Finalize && !source_.CardPresent()) {
        Fail(LoadError::CardAbsent);
        return false;
    }
    switch(status_.state) {
        case LoadState::Opening: {
            if(!source_.Open(entry_.path, file_size_)) {
                Fail(LoadError::Open);
                return false;
            }
            open_ = true;
            if(file_size_ < kHeaderSize + kMinPayload || file_size_ > kHeaderSize + kTargetSize) {
                Fail(LoadError::FileSize);
                return false;
            }
            status_.state = LoadState::Header;
            return true;
        }
        case LoadState::Header: {
            if(!ReadExact(header_bytes_, kHeaderSize)) {
                Fail(LoadError::HeaderRead);
                return false;
            }
            status_.image_error = ParseHeader(header_bytes_, kHeaderSize, header_);
            if(status_.image_error != ImageError::None) {
                Fail(LoadError::Header);
                return false;
            }
            if(std::strcmp(header_.product, entry_.id) != 0) {
                Fail(LoadError::Product);
                return false;
            }
            if(std::memcmp(header_.payload_sha256, entry_.payload_sha256, 32) != 0) {
                Fail(LoadError::HashMismatch);
                return false;
            }
            if(file_size_ != kHeaderSize + header_.payload_length) {
                Fail(LoadError::FileSize);
                return false;
            }
            if(header_.payload_length > target_.Capacity()) {
                Fail(LoadError::FileSize);
                return false;
            }
            // Whole target zeroed first: no remains of a previous program become the
            // start state of this one (CONTRACT.md 7.3). Cleaned together with the image.
            std::memset(target_.Base(), 0, target_.Capacity());
            status_.bytes_total = header_.payload_length;
            status_.bytes_done = 0;
            pm_sha256_init(&sha_);
            status_.state = LoadState::Payload;
            return true;
        }
        case LoadState::Payload: {
            uint8_t* base = target_.Base();
            if(status_.bytes_done < status_.bytes_total) {
                const uint32_t want = status_.bytes_total - status_.bytes_done;
                const uint32_t len = want < kChunk ? want : kChunk;
                const int32_t n = source_.Read(base + status_.bytes_done, len);
                if(n < 0) {
                    Fail(LoadError::Read);
                    return false;
                }
                if(n == 0) {
                    Fail(LoadError::Short);
                    return false;
                }
                pm_sha256_update(&sha_, base + status_.bytes_done, static_cast<size_t>(n));
                status_.bytes_done += static_cast<uint32_t>(n);
                return true;
            }
            // Exactly payload_length bytes consumed: the file must end here.
            uint8_t extra;
            const int32_t n = source_.Read(&extra, 1);
            if(n < 0) {
                Fail(LoadError::Read);
                return false;
            }
            if(n > 0) {
                Fail(LoadError::Long);
                return false;
            }
            status_.state = LoadState::Verify;
            return true;
        }
        case LoadState::Verify: {
            uint8_t digest[PM_SHA256_DIGEST_SIZE];
            pm_sha256_final(&sha_, digest);
            if(std::memcmp(digest, header_.payload_sha256, sizeof(digest)) != 0) {
                Fail(LoadError::Hash);
                return false;
            }
            status_.vector_error = CheckVectors(target_.Base(), status_.bytes_total);
            if(status_.vector_error != VectorError::None) {
                Fail(LoadError::Vector);
                return false;
            }
            source_.Close();
            open_ = false;
            status_.state = LoadState::Finalize;
            return true;
        }
        case LoadState::Finalize: {
            // Fixed order: nothing may write memory after Quiesce; dirty lines of the
            // whole (zeroed + loaded) target reach RAM before the bootloader's jump.
            commit_.Quiesce();
            target_.Clean(target_.Capacity());
            status_.state = LoadState::Committed;
            commit_.Commit(kTargetBase);  // no return on hardware
            return false;
        }
        default: return false;
    }
}

} // namespace pm

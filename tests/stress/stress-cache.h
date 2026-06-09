#pragma once

#include "testing.h"

#include "core/blob.h"

#include <cstdint>
#include <map>
#include <vector>

namespace rhi::testing::stress {

class InstrumentedCache : public IPersistentCache
{
public:
    struct Stats
    {
        uint64_t writeCount = 0;
        uint64_t queryCount = 0;
        uint64_t missCount = 0;
        uint64_t hitCount = 0;
        uint64_t entryCount = 0;
        uint64_t evictCount = 0;
        uint64_t bytesWritten = 0;
        uint64_t bytesReturned = 0;
    };

    using Key = std::vector<uint8_t>;
    using Data = std::vector<uint8_t>;

    void clear()
    {
        entries.clear();
        stats = {};
    }

    void evictTo(size_t maxEntries)
    {
        while (entries.size() > maxEntries)
        {
            entries.erase(entries.begin());
            stats.evictCount++;
        }
        stats.entryCount = entries.size();
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL writeCache(ISlangBlob* key_, ISlangBlob* data_) override
    {
        stats.writeCount++;
        Key key(
            static_cast<const uint8_t*>(key_->getBufferPointer()),
            static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize()
        );
        Data data(
            static_cast<const uint8_t*>(data_->getBufferPointer()),
            static_cast<const uint8_t*>(data_->getBufferPointer()) + data_->getBufferSize()
        );
        stats.bytesWritten += data.size();
        entries[key] = std::move(data);
        stats.entryCount = entries.size();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryCache(ISlangBlob* key_, ISlangBlob** outData) override
    {
        stats.queryCount++;
        Key key(
            static_cast<const uint8_t*>(key_->getBufferPointer()),
            static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize()
        );
        auto it = entries.find(key);
        if (it == entries.end())
        {
            stats.missCount++;
            *outData = nullptr;
            return SLANG_E_NOT_FOUND;
        }

        stats.hitCount++;
        stats.bytesReturned += it->second.size();
        *outData = UnownedBlob::create(it->second.data(), it->second.size()).detach();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) override
    {
        if (uuid == IPersistentCache::getTypeGuid())
        {
            *outObject = static_cast<IPersistentCache*>(this);
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }

    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 2; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 2; }

    std::map<Key, Data> entries;
    Stats stats;
};

} // namespace rhi::testing::stress

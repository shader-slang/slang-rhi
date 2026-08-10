#pragma once

#include <slang-rhi.h>

#include "../src/core/blob.h"

#include <vector>
#include <map>
#include <cstdint>
#include <mutex>

namespace rhi::testing {

class ShaderCache : public IPersistentCache
{
public:
    using Key = std::vector<uint8_t>;
    using Data = std::vector<uint8_t>;

    virtual SLANG_NO_THROW Result SLANG_MCALL writeCache(ISlangBlob* key_, ISlangBlob* data_) override
    {
        Key key(
            static_cast<const uint8_t*>(key_->getBufferPointer()),
            static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize()
        );
        Data data(
            static_cast<const uint8_t*>(data_->getBufferPointer()),
            static_cast<const uint8_t*>(data_->getBufferPointer()) + data_->getBufferSize()
        );
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries[key] = data;
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryCache(ISlangBlob* key_, ISlangBlob** outData) override
    {
        Key key(
            static_cast<const uint8_t*>(key_->getBufferPointer()),
            static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize()
        );
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        if (it == m_entries.end())
        {
            *outData = nullptr;
            return SLANG_E_NOT_FOUND;
        }
        *outData = OwnedBlob::create(it->second.data(), it->second.size()).detach();
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

    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override
    {
        // The lifetime of this object is tied to the test.
        // Do not perform any reference counting.
        return 2;
    }

    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override
    {
        // Returning 2 is important here, because when releasing a COM pointer, it checks
        // if the ref count **was 1 before releasing** in order to free the object.
        return 2;
    }

private:
    std::mutex m_mutex;
    std::map<Key, Data> m_entries;
};

} // namespace rhi::testing

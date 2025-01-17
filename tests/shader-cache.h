#pragma once

#include <slang-rhi.h>

#include "../src/core/blob.h"

#include <vector>
#include <map>
#include <cstdint>

namespace rhi::testing {

class ShaderCache : public IPersistentShaderCache
{
public:
    using Key = std::vector<uint8_t>;
    using Data = std::vector<uint8_t>;

    std::map<Key, Data> entries;

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
        entries[key] = data;
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryCache(ISlangBlob* key_, ISlangBlob** outData) override
    {
        Key key(
            static_cast<const uint8_t*>(key_->getBufferPointer()),
            static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize()
        );
        auto it = entries.find(key);
        if (it == entries.end())
        {
            *outData = nullptr;
            return SLANG_E_NOT_FOUND;
        }
        *outData = UnownedBlob::create(it->second.data(), it->second.size()).detach();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) override
    {
        if (uuid == IPersistentShaderCache::getTypeGuid())
        {
            *outObject = static_cast<IPersistentShaderCache*>(this);
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
};

} // namespace rhi::testing

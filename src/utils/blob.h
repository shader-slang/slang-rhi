#pragma once

#include "slang.h"
#include "com-object.h"

#include <vector>
#include <span>

namespace gfx {

/** Base class for simple blobs.
*/
class BlobBase : public ISlangBlob, public ISlangCastable, public ComBaseObject
{
public:
    // ISlangUnknown
    SLANG_COM_BASE_IUNKNOWN_ALL

    // ISlangCastable
    virtual SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& guid) SLANG_OVERRIDE;

protected:
    ISlangUnknown* getInterface(const Guid& guid);

    void* getObject(const Guid& guid);
};

class OwnedBlob : public BlobBase
{
public:
    virtual SLANG_NO_THROW void const* SLANG_MCALL getBufferPointer() { return m_data.data(); }
    virtual SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() { return m_data.size(); }

    static ComPtr<ISlangBlob> create(const void* data, size_t size) { return ComPtr<ISlangBlob>(new OwnedBlob(data, size)); }
    static ComPtr<ISlangBlob> create(std::span<const uint8_t> data) { return ComPtr<ISlangBlob>(new OwnedBlob(data)); }
    static ComPtr<ISlangBlob> moveCreate(std::vector<uint8_t>&& data) { return ComPtr<ISlangBlob>(new OwnedBlob(std::move(data))); } 
private:
    explicit OwnedBlob(const void* data, size_t size) : m_data((const uint8_t*)data, (const uint8_t*)data + size) {}
    explicit OwnedBlob(std::span<const uint8_t> data) : m_data(data.begin(), data.end()) {}
    explicit OwnedBlob(std::vector<uint8_t>&& data) : m_data(data) {}

    std::vector<uint8_t> m_data;
};

class UnownedBlob : public BlobBase
{
public:
    virtual SLANG_NO_THROW void const* SLANG_MCALL getBufferPointer() { return m_data; }
    virtual SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() { return m_size; }

    static ComPtr<ISlangBlob> create(const void* data, size_t size) { return ComPtr<ISlangBlob>(new UnownedBlob(data, size)); }
private:
    explicit UnownedBlob(const void* data, size_t size) : m_data(data), m_size(size) {}

    const void* m_data;
    size_t m_size;
};


} // namespace gfx

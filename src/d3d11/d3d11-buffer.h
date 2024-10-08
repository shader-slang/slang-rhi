#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class BufferImpl : public Buffer
{
public:
    BufferImpl(DeviceImpl* device, const BufferDesc& desc)
        : Buffer(desc)
        , m_device(device)
    {
    }

    DeviceImpl* m_device;
    MapFlavor m_mapFlavor;
    D3D11_USAGE m_d3dUsage;
    ComPtr<ID3D11Buffer> m_buffer;
    ComPtr<ID3D11Buffer> m_staging;
    std::vector<uint8_t> m_uploadStagingBuffer;

    struct ViewKey
    {
        Format format;
        BufferRange range;
        bool operator==(const ViewKey& other) const { return format == other.format && range == other.range; }
    };

    struct ViewKeyHasher
    {
        size_t operator()(const ViewKey& key) const
        {
            size_t hash = 0;
            hash_combine(hash, key.format);
            hash_combine(hash, key.range.offset);
            hash_combine(hash, key.range.size);
            return hash;
        }
    };

    std::unordered_map<ViewKey, ComPtr<ID3D11ShaderResourceView>, ViewKeyHasher> m_srvs;
    std::unordered_map<ViewKey, ComPtr<ID3D11UnorderedAccessView>, ViewKeyHasher> m_uavs;

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL map(BufferRange* rangeToRead, void** outPointer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(BufferRange* writtenRange) override;

    ID3D11ShaderResourceView* getSRV(Format format, const BufferRange& range);
    ID3D11UnorderedAccessView* getUAV(Format format, const BufferRange& range);
};

} // namespace rhi::d3d11

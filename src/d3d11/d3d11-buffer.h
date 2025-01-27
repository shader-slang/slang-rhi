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
    D3D11_USAGE m_d3dUsage;
    ComPtr<ID3D11Buffer> m_buffer;

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

    std::mutex m_mutex;
    std::unordered_map<ViewKey, ComPtr<ID3D11ShaderResourceView>, ViewKeyHasher> m_srvs;
    std::unordered_map<ViewKey, ComPtr<ID3D11UnorderedAccessView>, ViewKeyHasher> m_uavs;

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

    ID3D11ShaderResourceView* getSRV(Format format, const BufferRange& range);
    ID3D11UnorderedAccessView* getUAV(Format format, const BufferRange& range);
};

} // namespace rhi::d3d11

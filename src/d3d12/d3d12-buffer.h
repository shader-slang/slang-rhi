#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class BufferImpl : public Buffer
{
public:
    BufferImpl(Device* device, const BufferDesc& desc);

    ~BufferImpl();

    /// The resource in gpu memory, allocated on the correct heap relative to the cpu access flag
    D3D12Resource m_resource;
    D3D12_RESOURCE_STATES m_defaultState;

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL map(MemoryRange* rangeToRead, void** outPointer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(MemoryRange* writtenRange) override;

public:
    struct ViewKey
    {
        Format format;
        uint32_t stride;
        BufferRange range;
        bool operator==(const ViewKey& other) const
        {
            return format == other.format && stride == other.stride && range == other.range;
        }
    };

    struct ViewKeyHasher
    {
        size_t operator()(const ViewKey& key) const
        {
            size_t hash = 0;
            hash_combine(hash, key.format);
            hash_combine(hash, key.stride);
            hash_combine(hash, key.range.offset);
            hash_combine(hash, key.range.size);
            return hash;
        }
    };

    std::unordered_map<ViewKey, D3D12Descriptor, ViewKeyHasher> m_srvs;
    std::unordered_map<ViewKey, D3D12Descriptor, ViewKeyHasher> m_uavs;

    D3D12Descriptor getSRV(Format format, uint32_t stride, const BufferRange& range);
    D3D12Descriptor getUAV(Format format, uint32_t stride, const BufferRange& range);
};

} // namespace rhi::d3d12

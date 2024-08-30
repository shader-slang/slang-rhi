#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class BufferImpl : public Buffer
{
public:
    typedef Buffer Parent;

    BufferImpl(const IBuffer::Desc& desc)
        : Parent(desc)
    {
    }

    MapFlavor m_mapFlavor;
    D3D11_USAGE m_d3dUsage;
    ComPtr<ID3D11Buffer> m_buffer;
    ComPtr<ID3D11Buffer> m_staging;
    std::vector<uint8_t> m_uploadStagingBuffer;

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL map(MemoryRange* rangeToRead, void** outPointer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(MemoryRange* writtenRange) override;
};

} // namespace rhi::d3d11

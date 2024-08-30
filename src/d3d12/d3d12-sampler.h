#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class SamplerStateImpl : public SamplerStateBase
{
public:
    D3D12Descriptor m_descriptor;
    RefPtr<D3D12GeneralExpandingDescriptorHeap> m_allocator;
    ~SamplerStateImpl();
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(InteropHandle* outHandle) override;
};

} // namespace rhi::d3d12

#pragma once

#include "d3d12-base.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::d3d12 {

class TransientResourceHeapImpl : public TransientResourceHeapBaseImpl<DeviceImpl, BufferImpl>,
                                  public ITransientResourceHeapD3D12
{
private:
    typedef TransientResourceHeapBaseImpl<DeviceImpl, BufferImpl> Super;

public:
    // During command submission, we need all the descriptor tables that get
    // used to come from a single heap (for each descriptor heap type).
    //
    // We will thus keep a single heap of each type that we hope will hold
    // all the descriptors that actually get needed in a frame.
    short_vector<D3D12DescriptorHeap, 4> m_viewHeaps;    // Cbv, Srv, Uav
    short_vector<D3D12DescriptorHeap, 4> m_samplerHeaps; // Heap for samplers
    int32_t m_currentViewHeapIndex = -1;
    int32_t m_currentSamplerHeapIndex = -1;
    bool m_canResize = false;

    uint32_t m_viewHeapSize;
    uint32_t m_samplerHeapSize;

    D3D12DescriptorHeap& getCurrentViewHeap();
    D3D12DescriptorHeap& getCurrentSamplerHeap();

    D3D12LinearExpandingDescriptorHeap m_stagingCpuViewHeap;
    D3D12LinearExpandingDescriptorHeap m_stagingCpuSamplerHeap;

    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override;

    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return Super::addRef(); }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return Super::release(); }

    virtual SLANG_NO_THROW Result SLANG_MCALL allocateTransientDescriptorTable(
        DescriptorType type,
        GfxCount count,
        Offset& outDescriptorOffset,
        void** outD3DDescriptorHeapHandle
    ) override;

    ~TransientResourceHeapImpl();

    bool canResize() { return m_canResize; }

    Result init(
        const ITransientResourceHeap::Desc& desc,
        DeviceImpl* device,
        uint32_t viewHeapSize,
        uint32_t samplerHeapSize
    );

    Result allocateNewViewDescriptorHeap(DeviceImpl* device);

    Result allocateNewSamplerDescriptorHeap(DeviceImpl* device);

    Result synchronize();

    virtual SLANG_NO_THROW Result SLANG_MCALL synchronizeAndReset() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL finish() override;
};

} // namespace rhi::d3d12

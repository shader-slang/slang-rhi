#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

struct Submitter
{
    ID3D12Device* m_device;
    ID3D12GraphicsCommandList* m_commandList;

    Submitter(ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
        : m_device(device)
        , m_commandList(commandList)
    {
    }

    virtual void copyDescriptors(
        UINT count,
        D3D12_CPU_DESCRIPTOR_HANDLE dst,
        D3D12_CPU_DESCRIPTOR_HANDLE src,
        D3D12_DESCRIPTOR_HEAP_TYPE type
    );

    virtual void createConstantBufferView(
        D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation,
        UINT size,
        D3D12_CPU_DESCRIPTOR_HANDLE dst
    );

    virtual void setRootConstantBufferView(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation) = 0;
    virtual void setRootUAV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation) = 0;
    virtual void setRootSRV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation) = 0;
    virtual void setRootDescriptorTable(UINT index, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) = 0;
    virtual void setRootConstants(
        Index rootParamIndex,
        Index dstOffsetIn32BitValues,
        Index countOf32BitValues,
        void const* srcData
    ) = 0;
};

struct GraphicsSubmitter : public Submitter
{
    virtual void setRootConstantBufferView(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation) override;
    virtual void setRootUAV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation) override;
    virtual void setRootSRV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation) override;
    virtual void setRootDescriptorTable(UINT index, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor) override;
    virtual void setRootConstants(
        Index rootParamIndex,
        Index dstOffsetIn32BitValues,
        Index countOf32BitValues,
        void const* srcData
    ) override;

    GraphicsSubmitter(ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
        : Submitter(device, commandList)
    {
    }
};

struct ComputeSubmitter : public Submitter
{
    virtual void setRootConstantBufferView(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation) override;
    virtual void setRootUAV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation) override;
    virtual void setRootSRV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation) override;
    virtual void setRootDescriptorTable(UINT index, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor) override;
    virtual void setRootConstants(
        Index rootParamIndex,
        Index dstOffsetIn32BitValues,
        Index countOf32BitValues,
        void const* srcData
    ) override;

    ComputeSubmitter(ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
        : Submitter(device, commandList)
    {
    }
};

} // namespace rhi::d3d12

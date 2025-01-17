#include "d3d12-submitter.h"
#include "d3d12-pipeline.h"
#include "d3d12-helper-functions.h"

namespace rhi::d3d12 {

void Submitter::copyDescriptors(
    UINT count,
    D3D12_CPU_DESCRIPTOR_HANDLE dst,
    D3D12_CPU_DESCRIPTOR_HANDLE src,
    D3D12_DESCRIPTOR_HEAP_TYPE type
)
{
    m_device->CopyDescriptorsSimple(count, dst, src, type);
}

void Submitter::createConstantBufferView(
    D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation,
    UINT size,
    D3D12_CPU_DESCRIPTOR_HANDLE dst
)
{
    D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc = {};
    viewDesc.BufferLocation = gpuBufferLocation;
    viewDesc.SizeInBytes = size;
    m_device->CreateConstantBufferView(&viewDesc, dst);
}

void GraphicsSubmitter::setRootConstantBufferView(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetGraphicsRootConstantBufferView(index, gpuBufferLocation);
}

void GraphicsSubmitter::setRootUAV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetGraphicsRootUnorderedAccessView(index, gpuBufferLocation);
}

void GraphicsSubmitter::setRootSRV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetGraphicsRootShaderResourceView(index, gpuBufferLocation);
}

void GraphicsSubmitter::setRootDescriptorTable(UINT index, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
    m_commandList->SetGraphicsRootDescriptorTable(index, baseDescriptor);
}

void GraphicsSubmitter::setRootConstants(
    Index rootParamIndex,
    Index dstOffsetIn32BitValues,
    Index countOf32BitValues,
    const void* srcData
)
{
    m_commandList->SetGraphicsRoot32BitConstants(
        UINT(rootParamIndex),
        UINT(countOf32BitValues),
        srcData,
        UINT(dstOffsetIn32BitValues)
    );
}

void ComputeSubmitter::setRootConstantBufferView(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetComputeRootConstantBufferView(index, gpuBufferLocation);
}

void ComputeSubmitter::setRootUAV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetComputeRootUnorderedAccessView(index, gpuBufferLocation);
}

void ComputeSubmitter::setRootSRV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetComputeRootShaderResourceView(index, gpuBufferLocation);
}

void ComputeSubmitter::setRootDescriptorTable(UINT index, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
    m_commandList->SetComputeRootDescriptorTable(index, baseDescriptor);
}

void ComputeSubmitter::setRootConstants(
    Index rootParamIndex,
    Index dstOffsetIn32BitValues,
    Index countOf32BitValues,
    const void* srcData
)
{
    m_commandList->SetComputeRoot32BitConstants(
        UINT(rootParamIndex),
        UINT(countOf32BitValues),
        srcData,
        UINT(dstOffsetIn32BitValues)
    );
}

} // namespace rhi::d3d12

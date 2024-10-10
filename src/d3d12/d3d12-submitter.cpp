#include "d3d12-submitter.h"
#include "d3d12-pipeline.h"

namespace rhi::d3d12 {

void GraphicsSubmitter::setRootConstantBufferView(int index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetGraphicsRootConstantBufferView(index, gpuBufferLocation);
}

void GraphicsSubmitter::setRootUAV(int index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetGraphicsRootUnorderedAccessView(index, gpuBufferLocation);
}

void GraphicsSubmitter::setRootSRV(int index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetGraphicsRootShaderResourceView(index, gpuBufferLocation);
}

void GraphicsSubmitter::setRootDescriptorTable(int index, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
    m_commandList->SetGraphicsRootDescriptorTable(index, baseDescriptor);
}

void GraphicsSubmitter::setRootSignature(ID3D12RootSignature* rootSignature)
{
    m_commandList->SetGraphicsRootSignature(rootSignature);
}

void GraphicsSubmitter::setRootConstants(
    Index rootParamIndex,
    Index dstOffsetIn32BitValues,
    Index countOf32BitValues,
    void const* srcData
)
{
    m_commandList->SetGraphicsRoot32BitConstants(
        UINT(rootParamIndex),
        UINT(countOf32BitValues),
        srcData,
        UINT(dstOffsetIn32BitValues)
    );
}

void GraphicsSubmitter::setPipeline(Pipeline* pipeline)
{
    m_commandList->SetPipelineState(
        checked_cast<RenderPipelineImpl*>(pipeline->m_renderPipeline.get())->m_pipelineState.get()
    );
}

void ComputeSubmitter::setRootConstantBufferView(int index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetComputeRootConstantBufferView(index, gpuBufferLocation);
}

void ComputeSubmitter::setRootUAV(int index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetComputeRootUnorderedAccessView(index, gpuBufferLocation);
}

void ComputeSubmitter::setRootSRV(int index, D3D12_GPU_VIRTUAL_ADDRESS gpuBufferLocation)
{
    m_commandList->SetComputeRootShaderResourceView(index, gpuBufferLocation);
}

void ComputeSubmitter::setRootDescriptorTable(int index, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
    m_commandList->SetComputeRootDescriptorTable(index, baseDescriptor);
}

void ComputeSubmitter::setRootSignature(ID3D12RootSignature* rootSignature)
{
    m_commandList->SetComputeRootSignature(rootSignature);
}

void ComputeSubmitter::setRootConstants(
    Index rootParamIndex,
    Index dstOffsetIn32BitValues,
    Index countOf32BitValues,
    void const* srcData
)
{
    m_commandList->SetComputeRoot32BitConstants(
        UINT(rootParamIndex),
        UINT(countOf32BitValues),
        srcData,
        UINT(dstOffsetIn32BitValues)
    );
}

void ComputeSubmitter::setPipeline(Pipeline* pipeline)
{
    m_commandList->SetPipelineState(
        checked_cast<ComputePipelineImpl*>(pipeline->m_computePipeline.get())->m_pipelineState.get()
    );
}

} // namespace rhi::d3d12

#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class AdapterImpl : public Adapter
{
public:
    ComPtr<IDXGIAdapter> m_dxgiAdapter;
};

class DeviceImpl : public Device
{
public:
    using Device::readBuffer;

    DeviceImpl();
    ~DeviceImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createTexture(
        const TextureDesc& desc,
        const SubresourceData* initData,
        ITexture** outTexture
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureView(
        ITexture* texture,
        const TextureViewDesc& desc,
        ITextureView** outView
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createBuffer(
        const BufferDesc& desc,
        const void* initData,
        IBuffer** outBuffer
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmapBuffer(IBuffer* buffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(const SamplerDesc& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createInputLayout(
        const InputLayoutDesc& desc,
        IInputLayout** outLayout
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual Result createRootShaderObjectLayout(
        slang::IComponentType* program,
        slang::ProgramLayout* programLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createRenderPipeline2(
        const RenderPipelineDesc& desc,
        IRenderPipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createComputePipeline2(
        const ComputePipelineDesc& desc,
        IComputePipeline** outPipeline
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL readTexture(
        ITexture* texture,
        uint32_t layer,
        uint32_t mip,
        const SubresourceLayout& layout,
        void* outData
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL readBuffer(
        IBuffer* buffer,
        Offset offset,
        Size size,
        void* outData
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Format format, Size* outAlignment) override;


public:
    std::string m_adapterName;

    RefPtr<CommandQueueImpl> m_queue;

    ComPtr<IDXGIFactory> m_dxgiFactory;
    ComPtr<IDXGIAdapter> m_dxgiAdapter;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_immediateContext;
    ComPtr<ID3D11DeviceContext1> m_immediateContext1;
    ComPtr<ID3D11Query> m_disjointQuery;

#if SLANG_RHI_ENABLE_NVAPI
    NVAPIShaderExtension m_nvapiShaderExtension;
#endif
};

} // namespace rhi::d3d11

namespace rhi {

IAdapter* getD3D11Adapter(uint32_t index);
Result createD3D11Device(const DeviceDesc* desc, IDevice** outDevice);

} // namespace rhi

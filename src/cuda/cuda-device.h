#pragma once

#include "cuda-base.h"
#include "cuda-command-buffer.h"
#include "cuda-context.h"
#include "cuda-helper-functions.h"

namespace rhi::cuda {

class DeviceImpl : public RendererBase
{
private:
    static const CUDAReportStyle reportType = CUDAReportStyle::Normal;
    static int _calcSMCountPerMultiProcessor(int major, int minor);

    static Result _findMaxFlopsDeviceIndex(int* outDeviceIndex);

    static Result _initCuda(CUDAReportStyle reportType = CUDAReportStyle::Normal);

private:
    int m_deviceIndex = -1;
    CUdevice m_device = 0;
    RefPtr<CUDAContext> m_context;
    DeviceInfo m_info;
    std::string m_adapterName;

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(NativeHandles* outHandles) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const Desc& desc) override;

    Result getCUDAFormat(Format format, CUarray_format* outFormat);

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBuffer(const BufferDesc& descIn, const void* initData, IBuffer** outBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureFromSharedHandle(
        NativeHandle handle,
        const TextureDesc& desc,
        const size_t size,
        ITexture** outTexture
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureView(ITexture* texture, IResourceView::Desc const& desc, IResourceView** outView) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferView(IBuffer* buffer, IBuffer* counterBuffer, IResourceView::Desc const& desc, IResourceView** outView)
        override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createQueryPool(const IQueryPool::Desc& desc, IQueryPool** outPool) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayoutBase** outLayout
    ) override;

    virtual Result createShaderObject(ShaderObjectLayoutBase* layout, IShaderObject** outObject) override;

    virtual Result createMutableShaderObject(ShaderObjectLayoutBase* layout, IShaderObject** outObject) override;

    Result createRootShaderObject(IShaderProgram* program, ShaderObjectBase** outObject);

    virtual SLANG_NO_THROW Result SLANG_MCALL createProgram(
        const IShaderProgram::Desc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline(const ComputePipelineDesc& desc, IComputePipeline** outPipeline) override;

    void* map(IBuffer* buffer);

    void unmap(IBuffer* buffer);

    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override;

public:
    using TransientResourceHeapImpl = SimpleTransientResourceHeap<DeviceImpl, CommandBufferImpl>;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTransientResourceHeap(const ITransientResourceHeap::Desc& desc, ITransientResourceHeap** outHeap) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createCommandQueue(const ICommandQueue::Desc& desc, ICommandQueue** outQueue) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createSwapchain(const ISwapchain::Desc& desc, WindowHandle window, ISwapchain** outSwapchain) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createFramebufferLayout(const IFramebufferLayout::Desc& desc, IFramebufferLayout** outLayout) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createFramebuffer(const IFramebuffer::Desc& desc, IFramebuffer** outFramebuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPassLayout(const IRenderPassLayout::Desc& desc, IRenderPassLayout** outRenderPassLayout) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(SamplerDesc const& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(IInputLayout::Desc const& desc, IInputLayout** outLayout) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPipeline(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, ResourceState state, ISlangBlob** outBlob, size_t* outRowPitch, size_t* outPixelSize)
        override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readBuffer(IBuffer* buffer, size_t offset, size_t size, ISlangBlob** outBlob) override;
};

} // namespace rhi::cuda

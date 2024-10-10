#pragma once

#include "cuda-base.h"
#include "cuda-command-queue.h"
#include "cuda-command-buffer.h"
#include "cuda-helper-functions.h"

namespace rhi::cuda {

struct Context
{
    CUdevice device = -1;
    CUcontext context = nullptr;
#if SLANG_RHI_HAS_OPTIX
    OptixDeviceContext optixContext = nullptr;
#endif
};

class DeviceImpl : public Device
{
private:
    static const CUDAReportStyle reportType = CUDAReportStyle::Normal;
    static int _calcSMCountPerMultiProcessor(int major, int minor);

    static Result _findMaxFlopsDeviceIndex(int* outDeviceIndex);

    static Result _initCuda(CUDAReportStyle reportType = CUDAReportStyle::Normal);

public:
    Context m_ctx;
    DeviceInfo m_info;
    std::string m_adapterName;
    RefPtr<CommandQueueImpl> m_queue;

public:
    ~DeviceImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc) override;

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
    createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual Result createShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject) override;

    virtual Result createMutableShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject) override;

    Result createRootShaderObject(IShaderProgram* program, ShaderObjectBase** outObject);

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline2(const ComputePipelineDesc2& desc, IComputePipeline** outPipeline) override;

    void* map(IBuffer* buffer);

    void unmap(IBuffer* buffer);

    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override;

public:
    using TransientResourceHeapImpl = SimpleTransientResourceHeap<DeviceImpl, CommandBufferImpl>;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTransientResourceHeap(const ITransientResourceHeap::Desc& desc, ITransientResourceHeap** outHeap) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(SamplerDesc const& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPipeline(const RenderPipelineDesc& desc, IPipeline** outPipeline) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, ISlangBlob** outBlob, size_t* outRowPitch, size_t* outPixelSize) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readBuffer(IBuffer* buffer, size_t offset, size_t size, ISlangBlob** outBlob) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) override;
};

} // namespace rhi::cuda

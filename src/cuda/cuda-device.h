#pragma once

#include "cuda-base.h"
#include "cuda-helper-functions.h"

namespace rhi::cuda {

struct Context
{
    CUdevice device = -1;
    CUcontext context = nullptr;
#if SLANG_RHI_ENABLE_OPTIX
    OptixDeviceContext optixContext = nullptr;
#endif
};

class DeviceImpl : public Device
{
private:
    static const CUDAReportStyle kReportType = CUDAReportStyle::Normal;

    static int _calcSMCountPerMultiProcessor(int major, int minor);

    static Result _findMaxFlopsDeviceIndex(int* outDeviceIndex);

    static Result _initCuda(CUDAReportStyle reportType = CUDAReportStyle::Normal);

public:
    Context m_ctx;
    DeviceInfo m_info;
    std::string m_adapterName;
    RefPtr<CommandQueueImpl> m_queue;

public:
    DeviceImpl();
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

    virtual SLANG_NO_THROW Result SLANG_MCALL mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL unmapBuffer(IBuffer* buffer) override;

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

    virtual Result createRootShaderObjectLayout(
        slang::IComponentType* program,
        slang::ProgramLayout* programLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderTable(const ShaderTableDesc& desc, IShaderTable** outShaderTable) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline) override;

    void* map(IBuffer* buffer);

    void unmap(IBuffer* buffer);

    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override;

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(const SamplerDesc& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout) override;

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

    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFences(uint32_t fenceCount, IFence** fences, uint64_t* fenceValues, bool waitForAll, uint64_t timeout)
        override;

    void customizeShaderObject(ShaderObject* shaderObject) override;
};

} // namespace rhi::cuda

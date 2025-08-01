#pragma once

#include "cuda-base.h"
#include "cuda-clear-engine.h"
#include "cuda-dual-page-allocator.h"

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
    static int _calcSMCountPerMultiProcessor(int major, int minor);

    Result _findMaxFlopsDeviceIndex(int* outDeviceIndex);

    Result _initCuda();

public:
    Context m_ctx;
    std::string m_adapterName;
    RefPtr<CommandQueueImpl> m_queue;
    ClearEngine m_clearEngine;
    bool m_ownsContext = false;
    bool m_ownsOptixContext = false;
    DualPageAllocator m_dualPageAllocator;

public:
    using Device::readBuffer;

    DeviceImpl();
    ~DeviceImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createTexture(
        const TextureDesc& desc,
        const SubresourceData* initData,
        ITexture** outTexture
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createBuffer(
        const BufferDesc& desc,
        const void* initData,
        IBuffer** outBuffer
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createBufferFromSharedHandle(
        NativeHandle handle,
        const BufferDesc& desc,
        IBuffer** outBuffer
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL unmapBuffer(IBuffer* buffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureFromSharedHandle(
        NativeHandle handle,
        const TextureDesc& desc,
        const size_t size,
        ITexture** outTexture
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureView(
        ITexture* texture,
        const TextureViewDesc& desc,
        ITextureView** outView
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

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderTable(
        const ShaderTableDesc& desc,
        IShaderTable** outShaderTable
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createComputePipeline2(
        const ComputePipelineDesc& desc,
        IComputePipeline** outPipeline
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createRayTracingPipeline2(
        const RayTracingPipelineDesc& desc,
        IRayTracingPipeline** outPipeline
    ) override;

    void* map(IBuffer* buffer);

    void unmap(IBuffer* buffer);

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(const SamplerDesc& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createInputLayout(
        const InputLayoutDesc& desc,
        IInputLayout** outLayout
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
        size_t offset,
        size_t size,
        void* outData
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL waitForFences(
        uint32_t fenceCount,
        IFence** fences,
        const uint64_t* fenceValues,
        bool waitForAll,
        uint64_t timeout
    ) override;

    void customizeShaderObject(ShaderObject* shaderObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Format format, size_t* outAlignment) override;
};

} // namespace rhi::cuda

namespace rhi {

Result SLANG_MCALL getCUDAAdapters(std::vector<AdapterInfo>& outAdapters);
Result SLANG_MCALL createCUDADevice(const DeviceDesc* desc, IDevice** outDevice);

} // namespace rhi

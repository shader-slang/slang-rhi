#pragma once

#include "metal-base.h"
#include "metal-clear-engine.h"

#include <string>

namespace rhi::metal {

class AdapterImpl : public Adapter
{
public:
    NS::SharedPtr<MTL::Device> m_device;
};

class DeviceImpl : public Device
{
public:
    using Device::readBuffer;

    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;
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
    virtual SLANG_NO_THROW Result SLANG_MCALL createBufferFromNativeHandle(
        NativeHandle handle,
        const BufferDesc& desc,
        IBuffer** outBuffer
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(const SamplerDesc& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmapBuffer(IBuffer* buffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureView(
        ITexture* texture,
        const TextureViewDesc& desc,
        ITextureView** outView
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createInputLayout(
        const InputLayoutDesc& desc,
        IInputLayout** outLayout
    ) override;

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
    virtual SLANG_NO_THROW Result SLANG_MCALL createRenderPipeline2(
        const RenderPipelineDesc& desc,
        IRenderPipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createComputePipeline2(
        const ComputePipelineDesc& desc,
        IComputePipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createRayTracingPipeline2(
        const RayTracingPipelineDesc& desc,
        IRayTracingPipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;

    // Metal uses the generic implementation for now. But it might be worth bringing back the specific implementation
    // for better performance. The current implementation lacks support for layer/mipLevels though.
#if 0
    virtual SLANG_NO_THROW Result SLANG_MCALL readTexture(
        ITexture* texture,
        uint32_t layer,
        uint32_t mip,
        ISlangBlob** outBlob,
        Size* outRowPitch,
        Size* outPixelSize
    ) override;
#endif

    virtual SLANG_NO_THROW Result SLANG_MCALL readBuffer(
        IBuffer* buffer,
        Offset offset,
        Size size,
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

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureAllocationInfo(
        const TextureDesc& desc,
        Size* outSize,
        Size* outAlignment
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Format format, Size* outAlignment) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL waitForFences(
        uint32_t fenceCount,
        IFence** fences,
        const uint64_t* fenceValues,
        bool waitForAll,
        uint64_t timeout
    ) override;

    // void waitForGpu();
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;

    DeviceImpl();
    ~DeviceImpl();

public:
    std::string m_adapterName;

    bool captureEnabled() const { return std::getenv("MTL_CAPTURE_ENABLED") != nullptr; }

    NS::SharedPtr<MTL::Device> m_device;
    RefPtr<CommandQueueImpl> m_queue;
    NS::SharedPtr<MTL::CommandQueue> m_commandQueue;
    ClearEngine m_clearEngine;

    // Global registry of all acceleration structures.
    // IAccelerationStructure::getHandle will return the index into this array.
    // These indices are used when building instance acceleration structures.
    struct
    {
        std::vector<MTL::AccelerationStructure*> list;
        std::vector<uint32_t> freeList;
        NS::SharedPtr<NS::Array> array;
        bool dirty = true;
    } m_accelerationStructures;

    NS::Array* getAccelerationStructureArray();

    bool m_hasArgumentBufferTier2 = false;
};

} // namespace rhi::metal

namespace rhi {

IAdapter* getMetalAdapter(uint32_t index);
Result createMetalDevice(const DeviceDesc* desc, IDevice** outDevice);

} // namespace rhi

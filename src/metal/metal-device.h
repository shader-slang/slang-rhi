#pragma once

#include "metal-base.h"
#include "metal-clear-engine.h"

#include "metal-buffer-address-map.h"

#include <atomic>
#include <string>
#include <unordered_map>

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

    Result initialize(const DeviceDesc& desc, BackendImpl* backend);
    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createTexture(
        const TextureDesc& desc,
        const SubresourceData* initData,
        ITexture** outTexture
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureFromNativeHandle(
        NativeHandle handle,
        const TextureDesc& desc,
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

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;

    DeviceImpl();
    ~DeviceImpl();

    void deferDelete(Resource* resource);

public:
    std::string m_adapterName;

    bool captureEnabled() const { return std::getenv("MTL_CAPTURE_ENABLED") != nullptr; }

    NS::SharedPtr<MTL::Device> m_device;
    /// The single command queue. Device-level operations (readBuffer,
    /// createBuffer, createTexture) use m_queue->m_queueFence to participate
    /// in the fence chain. See synchronization model in metal-command.h.
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
        bool arrayDirty = true;
        std::vector<MTL::Resource*> resources;
        bool resourcesDirty = true;
    } m_accelerationStructures;

    uint32_t registerAccelerationStructure(MTL::AccelerationStructure* accelerationStructure);
    void unregisterAccelerationStructure(uint32_t index, MTL::AccelerationStructure* accelerationStructure);
    NS::Array* getAccelerationStructureArray();
    std::span<MTL::Resource* const> getAccelerationStructureResources();

    bool m_hasArgumentBufferTier2 = false;

    NS::SharedPtr<MTL::ResidencySet> m_residencySet;
    bool m_hasResidencySet = false;
    bool m_residencySetDirty = false;
    std::mutex m_residencySetMutex;
    std::unordered_map<MTL::Resource*, uint32_t> m_residencySetResourceRefCounts;

    // Fallback residency: maps GPU virtual addresses to their owning BufferImpl.
    // Only active when !m_hasResidencySet.
    BufferAddressMap m_addressToBuffer;

    void registerResource(MTL::Resource* resource);
    void unregisterResource(MTL::Resource* resource);

    std::atomic<bool> m_hasCommandBufferError{false};
};

} // namespace rhi::metal

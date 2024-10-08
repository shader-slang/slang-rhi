#pragma once

#include "../simple-transient-resource-heap.h"
#include "metal-base.h"
#include "metal-device.h"
#include "metal-command-queue.h"

#include "core/stable_vector.h"

#include <string>

namespace rhi::metal {

class DeviceImpl : public Device
{
public:
    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getFormatSupport(Format format, FormatSupport* outFormatSupport) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTransientResourceHeap(const ITransientResourceHeap::Desc& desc, ITransientResourceHeap** outHeap) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBuffer(const BufferDesc& desc, const void* initData, IBuffer** outBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(SamplerDesc const& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    ) override;
    virtual Result createShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject) override;
    virtual Result createMutableShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createMutableRootShaderObject(IShaderProgram* program, IShaderObject** outObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderTable(const IShaderTable::Desc& desc, IShaderTable** outShaderTable) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPipeline(const RenderPipelineDesc& desc, IPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline(const ComputePipelineDesc& desc, IPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRayTracingPipeline(const RayTracingPipelineDesc& desc, IPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, ISlangBlob** outBlob, Size* outRowPitch, Size* outPixelSize) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readBuffer(IBuffer* buffer, Offset offset, Size size, ISlangBlob** outBlob) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Size* outAlignment) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFences(GfxCount fenceCount, IFence** fences, uint64_t* fenceValues, bool waitForAll, uint64_t timeout)
        override;

    // void waitForGpu();
    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;
    ~DeviceImpl();

public:
    DeviceInfo m_info;
    std::string m_adapterName;

    bool captureEnabled() const { return std::getenv("MTL_CAPTURE") != nullptr; }

    DeviceDesc m_desc;
    NS::SharedPtr<MTL::Device> m_device;
    RefPtr<CommandQueueImpl> m_queue;
    NS::SharedPtr<MTL::CommandQueue> m_commandQueue;

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

    // A list to hold objects that may have a strong back reference to the device
    // instance. Because of the pipeline cache in `Device`, there could be a reference
    // cycle among `DeviceImpl`->`PipelineImpl`->`ShaderProgramImpl`->`DeviceImpl`.
    // Depending on whether a `PipelineState` objects gets stored in pipeline cache, there
    // may or may not be such a reference cycle.
    // We need to hold strong references to any objects that may become part of the reference
    // cycle here, so that when objects like `ShaderProgramImpl` lost all public refernces, we
    // can always safely break the strong reference in `ShaderProgramImpl::m_device` without
    // worrying the `ShaderProgramImpl` object getting destroyed after the completion of
    // `DeviceImpl::~DeviceImpl()'.
    stable_vector<RefPtr<RefObject>, 1024> m_deviceObjectsWithPotentialBackReferences;
};

} // namespace rhi::metal

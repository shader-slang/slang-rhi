#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugDevice : public DebugObject<IDevice>
{
public:
    Result SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) noexcept override;
    SLANG_COM_OBJECT_IUNKNOWN_ADD_REF;
    SLANG_COM_OBJECT_IUNKNOWN_RELEASE;

public:
    DebugDevice(IDebugCallback* debugCallback);
    IDevice* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasFeature(const char* feature) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getFeatures(const char** outFeatures, Size bufferSize, GfxCount* outFeatureCount) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getFormatSupport(Format format, FormatSupport* outFormatSupport) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSlangSession(slang::ISession** outSlangSession) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTransientResourceHeap(const ITransientResourceHeap::Desc& desc, ITransientResourceHeap** outHeap) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& srcDesc, ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureFromSharedHandle(
        NativeHandle handle,
        const TextureDesc& srcDesc,
        const Size size,
        ITexture** outTexture
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBuffer(const BufferDesc& desc, const void* initData, IBuffer** outBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(SamplerDesc const& desc, ISampler** outSampler) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderObject(
        slang::TypeReflection* type,
        ShaderObjectContainerType container,
        IShaderObject** outObject
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderObject2(
        slang::ISession* session,
        slang::TypeReflection* type,
        ShaderObjectContainerType container,
        IShaderObject** outObject
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createMutableShaderObject(
        slang::TypeReflection* type,
        ShaderObjectContainerType container,
        IShaderObject** outObject
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createMutableShaderObject2(
        slang::ISession* session,
        slang::TypeReflection* type,
        ShaderObjectContainerType container,
        IShaderObject** outObject
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderObjectFromTypeLayout(slang::TypeLayoutReflection* typeLayout, IShaderObject** outObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createMutableShaderObjectFromTypeLayout(
        slang::TypeLayoutReflection* typeLayout,
        IShaderObject** outObject
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createMutableRootShaderObject(IShaderProgram* program, IShaderObject** outObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnostics
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPipeline(const RenderPipelineDesc& desc, IPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline(const ComputePipelineDesc& desc, IPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRayTracingPipeline(const RayTracingPipelineDesc& desc, IPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPipeline2(const RenderPipelineDesc2& desc, IRenderPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline2(const ComputePipelineDesc2& desc, IComputePipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRayTracingPipeline2(const RayTracingPipelineDesc2& desc, IRayTracingPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, ISlangBlob** outBlob, Size* outRowPitch, Size* outPixelSize) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    readBuffer(IBuffer* buffer, Offset offset, Size size, ISlangBlob** outBlob) override;
    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFences(GfxCount fenceCount, IFence** fences, uint64_t* values, bool waitForAll, uint64_t timeout) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getTextureAllocationInfo(const TextureDesc& desc, size_t* outSize, size_t* outAlignment) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(size_t* outAlignment) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderTable(const IShaderTable::Desc& desc, IShaderTable** outTable) override;

private:
    DebugContext m_ctx;
};

} // namespace rhi::debug

#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugDevice : public DebugObject<IDevice>
{
public:
    Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) noexcept override;
    SLANG_COM_OBJECT_IUNKNOWN_ADD_REF;
    SLANG_COM_OBJECT_IUNKNOWN_RELEASE;

public:
    DebugDevice(DeviceType deviceType, IDebugCallback* debugCallback);
    IDevice* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getFeatures(uint32_t* outFeatureCount, Feature* outFeatures) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasFeature(Feature feature) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasFeature(const char* feature) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getCapabilities(
        uint32_t* outCapabilityCount,
        Capability* outCapabilities
    ) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasCapability(Capability capability) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasCapability(const char* capability) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getFormatSupport(Format format, FormatSupport* outFormatSupport) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSlangSession(slang::ISession** outSlangSession) override;
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
    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureFromSharedHandle(
        NativeHandle handle,
        const TextureDesc& desc,
        const Size size,
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
    virtual SLANG_NO_THROW Result SLANG_MCALL createBufferFromSharedHandle(
        NativeHandle handle,
        const BufferDesc& desc,
        IBuffer** outBuffer
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmapBuffer(IBuffer* buffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(const SamplerDesc& desc, ISampler** outSampler) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureView(
        ITexture* texture,
        const TextureViewDesc& desc,
        ITextureView** outView
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createInputLayout(
        const InputLayoutDesc& desc,
        IInputLayout** outLayout
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderObject(
        slang::ISession* session,
        slang::TypeReflection* type,
        ShaderObjectContainerType container,
        IShaderObject** outObject
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderObjectFromTypeLayout(
        slang::TypeLayoutReflection* typeLayout,
        IShaderObject** outObject
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createRootShaderObject(
        IShaderProgram* program,
        IShaderObject** outObject
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnostics
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createRenderPipeline(
        const RenderPipelineDesc& desc,
        IRenderPipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createComputePipeline(
        const ComputePipelineDesc& desc,
        IComputePipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createRayTracingPipeline(
        const RayTracingPipelineDesc& desc,
        IRayTracingPipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getCompilationReportList(ISlangBlob** outReportListBlob) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL readTexture(
        ITexture* texture,
        uint32_t layer,
        uint32_t mip,
        const SubresourceLayout& layout,
        void* outData
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL readTexture(
        ITexture* texture,
        uint32_t layer,
        uint32_t mip,
        ISlangBlob** outBlob,
        SubresourceLayout* outLayout
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL readBuffer(
        IBuffer* buffer,
        Offset offset,
        Size size,
        void* outData
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL readBuffer(
        IBuffer* buffer,
        Offset offset,
        Size size,
        ISlangBlob** outBlob
    ) override;
    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getInfo() const override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL waitForFences(
        uint32_t fenceCount,
        IFence** fences,
        const uint64_t* fenceValues,
        bool waitForAll,
        uint64_t timeout
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createHeap(const HeapDesc& desc, IHeap** outHeap) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureAllocationInfo(
        const TextureDesc& desc,
        size_t* outSize,
        size_t* outAlignment
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Format format, size_t* outAlignment) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getCooperativeVectorProperties(
        CooperativeVectorProperties* properties,
        uint32_t* propertiesCount
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getCooperativeVectorMatrixSize(
        uint32_t rowCount,
        uint32_t colCount,
        CooperativeVectorComponentType componentType,
        CooperativeVectorMatrixLayout layout,
        size_t rowColumnStride,
        size_t* outSize
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL convertCooperativeVectorMatrix(
        void* dstBuffer,
        size_t dstBufferSize,
        const CooperativeVectorMatrixDesc* dstDescs,
        const void* srcBuffer,
        size_t srcBufferSize,
        const CooperativeVectorMatrixDesc* srcDescs,
        uint32_t matrixCount
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderTable(
        const ShaderTableDesc& desc,
        IShaderTable** outTable
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL reportHeaps(HeapReport* heapReports, uint32_t* heapCount) override;

private:
    DebugContext m_ctx;
};

} // namespace rhi::debug

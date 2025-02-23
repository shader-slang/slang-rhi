#include "debug-device.h"
#include "debug-command-queue.h"
#include "debug-fence.h"
#include "debug-helper-functions.h"
#include "debug-query.h"
#include "debug-shader-object.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::debug {

Result DebugDevice::queryInterface(const SlangUUID& uuid, void** outObject) noexcept
{
    void* intf = getInterface(uuid);
    if (intf)
    {
        addRef();
        *outObject = intf;
        return SLANG_OK;
    }

    // Fallback to trying to get the interface from the debugged object
    return baseObject->queryInterface(uuid, outObject);
}

Result DebugDevice::getNativeDeviceHandles(DeviceNativeHandles* outHandles)
{
    return baseObject->getNativeDeviceHandles(outHandles);
}

Result DebugDevice::getFeatures(const char** outFeatures, size_t bufferSize, uint32_t* outFeatureCount)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getFeatures(outFeatures, bufferSize, outFeatureCount);
}

Result DebugDevice::getFormatSupport(Format format, FormatSupport* outFormatSupport)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getFormatSupport(format, outFormatSupport);
}

DebugDevice::DebugDevice(IDebugCallback* debugCallback)
    : DebugObject(&m_ctx)
{
    ctx->debugCallback = debugCallback;
    SLANG_RHI_API_FUNC_NAME("CreateDevice");
    RHI_VALIDATION_INFO("Debug layer is enabled.");
}

bool DebugDevice::hasFeature(const char* feature)
{
    SLANG_RHI_API_FUNC;

    return baseObject->hasFeature(feature);
}

Result DebugDevice::getSlangSession(slang::ISession** outSlangSession)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getSlangSession(outSlangSession);
}

Result DebugDevice::createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture)
{
    SLANG_RHI_API_FUNC;

    TextureDesc patchedDesc = fixupTextureDesc(desc);
    std::string label;
    if (!patchedDesc.label)
    {
        label = createTextureLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }
    return baseObject->createTexture(patchedDesc, initData, outTexture);
}

Result DebugDevice::createTextureFromNativeHandle(
    NativeHandle handle,
    const TextureDesc& srcDesc,
    ITexture** outTexture
)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createTextureFromNativeHandle(handle, srcDesc, outTexture);
}

Result DebugDevice::createTextureFromSharedHandle(
    NativeHandle handle,
    const TextureDesc& srcDesc,
    const size_t size,
    ITexture** outTexture
)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createTextureFromSharedHandle(handle, srcDesc, size, outTexture);
}

Result DebugDevice::createBuffer(const BufferDesc& desc, const void* initData, IBuffer** outBuffer)
{
    SLANG_RHI_API_FUNC;

    BufferDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createBufferLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }
    return baseObject->createBuffer(patchedDesc, initData, outBuffer);
}

Result DebugDevice::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createBufferFromNativeHandle(handle, srcDesc, outBuffer);
}

Result DebugDevice::createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createBufferFromSharedHandle(handle, srcDesc, outBuffer);
}

Result DebugDevice::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    SLANG_RHI_API_FUNC;

    switch (mode)
    {
    case CpuAccessMode::Read:
        if (buffer->getDesc().memoryType != MemoryType::ReadBack)
        {
            RHI_VALIDATION_ERROR("Buffer must be created with MemoryType::ReadBack to map with CpuAccessMode::Read");
            return SLANG_E_INVALID_ARG;
        }
        break;
    case CpuAccessMode::Write:
        if (buffer->getDesc().memoryType != MemoryType::Upload)
        {
            RHI_VALIDATION_ERROR("Buffer must be created with MemoryType::Upload to map with CpuAccessMode::Write");
            return SLANG_E_INVALID_ARG;
        }
        break;
    default:
        RHI_VALIDATION_ERROR("Invalid CpuAccessMode");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->mapBuffer(buffer, mode, outData);
}

Result DebugDevice::unmapBuffer(IBuffer* buffer)
{
    SLANG_RHI_API_FUNC;

    return baseObject->unmapBuffer(buffer);
}

Result DebugDevice::createSampler(const SamplerDesc& desc, ISampler** outSampler)
{
    SLANG_RHI_API_FUNC;

    SamplerDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createSamplerLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }
    return baseObject->createSampler(patchedDesc, outSampler);
}

Result DebugDevice::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    SLANG_RHI_API_FUNC;

    TextureViewDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createTextureViewLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }
    return baseObject->createTextureView(texture, patchedDesc, outView);
}

Result DebugDevice::getAccelerationStructureSizes(
    const AccelerationStructureBuildDesc& desc,
    AccelerationStructureSizes* outSizes
)
{
    SLANG_RHI_API_FUNC;
    validateAccelerationStructureBuildDesc(ctx, desc);
    return baseObject->getAccelerationStructureSizes(desc, outSizes);
}

Result DebugDevice::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createAccelerationStructure(desc, outAccelerationStructure);
}

Result DebugDevice::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugSurface> outObject = new DebugSurface(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->createSurface(windowHandle, outObject->baseObject.writeRef()));
    returnComPtr(outSurface, outObject);
    return SLANG_OK;
}

Result DebugDevice::createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createInputLayout(desc, outLayout);
}

Result DebugDevice::getQueue(QueueType type, ICommandQueue** outQueue)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugCommandQueue> outObject = new DebugCommandQueue(ctx);
    auto result = baseObject->getQueue(type, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outQueue, outObject);
    return result;
}

Result DebugDevice::createShaderObject(
    slang::ISession* session,
    slang::TypeReflection* type,
    ShaderObjectContainerType containerType,
    IShaderObject** outShaderObject
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugShaderObject> outObject = new DebugShaderObject(ctx);
    auto result = baseObject->createShaderObject(session, type, containerType, outObject->baseObject.writeRef());
    outObject->m_typeName = string::from_cstr(type->getName());
    outObject->m_device = this;
    outObject->m_slangType = type;
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outShaderObject, outObject);
    return result;
}

Result DebugDevice::createShaderObjectFromTypeLayout(
    slang::TypeLayoutReflection* typeLayout,
    IShaderObject** outShaderObject
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugShaderObject> outObject = new DebugShaderObject(ctx);
    auto result = baseObject->createShaderObjectFromTypeLayout(typeLayout, outObject->baseObject.writeRef());
    auto type = typeLayout->getType();
    outObject->m_typeName = string::from_cstr(type->getName());
    outObject->m_device = this;
    outObject->m_slangType = type;
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outShaderObject, outObject);
    return result;
}

Result DebugDevice::createRootShaderObject(IShaderProgram* program, IShaderObject** outObject)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugShaderObject> outRootObject = new DebugShaderObject(ctx);
    auto result = baseObject->createRootShaderObject(program, outRootObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outObject, outRootObject);
    return result;
}

Result DebugDevice::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnostics
)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createShaderProgram(desc, outProgram, outDiagnostics);
}

Result DebugDevice::createRenderPipeline(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createRenderPipeline(desc, outPipeline);
}

Result DebugDevice::createComputePipeline(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createComputePipeline(desc, outPipeline);
}

Result DebugDevice::createRayTracingPipeline(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createRayTracingPipeline(desc, outPipeline);
}

Result DebugDevice::readTexture(ITexture* texture, ISlangBlob** outBlob, size_t* outRowPitch, size_t* outPixelSize)
{
    SLANG_RHI_API_FUNC;
    return baseObject->readTexture(texture, outBlob, outRowPitch, outPixelSize);
}

Result DebugDevice::readBuffer(IBuffer* buffer, size_t offset, size_t size, ISlangBlob** outBlob)
{
    SLANG_RHI_API_FUNC;
    return baseObject->readBuffer(buffer, offset, size, outBlob);
}

const DeviceInfo& DebugDevice::getDeviceInfo() const
{
    SLANG_RHI_API_FUNC;
    return baseObject->getDeviceInfo();
}

Result DebugDevice::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    SLANG_RHI_API_FUNC;
    RefPtr<DebugQueryPool> result = new DebugQueryPool(ctx);
    result->desc = desc;
    SLANG_RETURN_ON_FAIL(baseObject->createQueryPool(desc, result->baseObject.writeRef()));
    returnComPtr(outPool, result);
    return SLANG_OK;
}

Result DebugDevice::createFence(const FenceDesc& desc, IFence** outFence)
{
    SLANG_RHI_API_FUNC;
    RefPtr<DebugFence> result = new DebugFence(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->createFence(desc, result->baseObject.writeRef()));
    returnComPtr(outFence, result);
    return SLANG_OK;
}

Result DebugDevice::waitForFences(
    uint32_t fenceCount,
    IFence** fences,
    uint64_t* values,
    bool waitForAll,
    uint64_t timeout
)
{
    SLANG_RHI_API_FUNC;
    short_vector<IFence*> innerFences;
    for (uint32_t i = 0; i < fenceCount; i++)
    {
        innerFences.push_back(getInnerObj(fences[i]));
    }
    return baseObject->waitForFences(fenceCount, innerFences.data(), values, waitForAll, timeout);
}

Result DebugDevice::getTextureAllocationInfo(const TextureDesc& desc, size_t* outSize, size_t* outAlignment)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getTextureAllocationInfo(desc, outSize, outAlignment);
}

Result DebugDevice::getTextureRowAlignment(size_t* outAlignment)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getTextureRowAlignment(outAlignment);
}

Result DebugDevice::getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertyCount)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getCooperativeVectorProperties(properties, propertyCount);
}

Result DebugDevice::convertCooperativeVectorMatrix(const ConvertCooperativeVectorMatrixDesc* descs, uint32_t descCount)
{
    SLANG_RHI_API_FUNC;
    return baseObject->convertCooperativeVectorMatrix(descs, descCount);
}

Result DebugDevice::createShaderTable(const ShaderTableDesc& desc, IShaderTable** outTable)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createShaderTable(desc, outTable);
}

} // namespace rhi::debug

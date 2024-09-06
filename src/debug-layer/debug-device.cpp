#include "debug-device.h"
#include "debug-buffer.h"
#include "debug-command-queue.h"
#include "debug-fence.h"
#include "debug-framebuffer.h"
#include "debug-helper-functions.h"
#include "debug-pipeline.h"
#include "debug-query.h"
#include "debug-resource-views.h"
#include "debug-sampler.h"
#include "debug-shader-object.h"
#include "debug-shader-program.h"
#include "debug-shader-table.h"
#include "debug-swap-chain.h"
#include "debug-texture.h"
#include "debug-transient-heap.h"
#include "debug-vertex-layout.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::debug {

Result DebugDevice::queryInterface(SlangUUID const& uuid, void** outObject) noexcept
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

Result DebugDevice::getNativeDeviceHandles(NativeHandles* outHandles)
{
    return baseObject->getNativeDeviceHandles(outHandles);
}

Result DebugDevice::getFeatures(const char** outFeatures, Size bufferSize, GfxCount* outFeatureCount)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getFeatures(outFeatures, bufferSize, outFeatureCount);
}

Result DebugDevice::getFormatSupportedResourceStates(Format format, ResourceStateSet* outStates)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getFormatSupportedResourceStates(format, outStates);
}

DebugDevice::DebugDevice()
{
    SLANG_RHI_API_FUNC_NAME("CreateDevice");
    RHI_VALIDATION_INFO("Debug layer is enabled.");
}

SLANG_NO_THROW bool SLANG_MCALL DebugDevice::hasFeature(const char* feature)
{
    SLANG_RHI_API_FUNC;

    return baseObject->hasFeature(feature);
}

Result DebugDevice::getSlangSession(slang::ISession** outSlangSession)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getSlangSession(outSlangSession);
}

Result DebugDevice::createTransientResourceHeap(
    const ITransientResourceHeap::Desc& desc,
    ITransientResourceHeap** outHeap
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugTransientResourceHeap> outObject = new DebugTransientResourceHeap();
    auto result = baseObject->createTransientResourceHeap(desc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outHeap, outObject);
    return result;
}

Result DebugDevice::createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugTexture> outObject = new DebugTexture();
    auto result = baseObject->createTexture(desc, initData, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outTexture, outObject);
    return result;
}

Result DebugDevice::createTextureFromNativeHandle(
    NativeHandle handle,
    const TextureDesc& srcDesc,
    ITexture** outTexture
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugTexture> outObject = new DebugTexture();
    auto result = baseObject->createTextureFromNativeHandle(handle, srcDesc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outTexture, outObject);
    return result;
}

Result DebugDevice::createTextureFromSharedHandle(
    NativeHandle handle,
    const TextureDesc& srcDesc,
    const size_t size,
    ITexture** outTexture
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugTexture> outObject = new DebugTexture();
    auto result = baseObject->createTextureFromSharedHandle(handle, srcDesc, size, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outTexture, outObject);
    return result;
}

Result DebugDevice::createBuffer(const BufferDesc& desc, const void* initData, IBuffer** outBuffer)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugBuffer> outObject = new DebugBuffer();
    auto result = baseObject->createBuffer(desc, initData, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outBuffer, outObject);
    return result;
}

Result DebugDevice::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugBuffer> outObject = new DebugBuffer();
    auto result = baseObject->createBufferFromNativeHandle(handle, srcDesc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outBuffer, outObject);
    return result;
}

Result DebugDevice::createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugBuffer> outObject = new DebugBuffer();
    auto result = baseObject->createBufferFromSharedHandle(handle, srcDesc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outBuffer, outObject);
    return result;
}

Result DebugDevice::createSampler(SamplerDesc const& desc, ISampler** outSampler)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugSampler> outObject = new DebugSampler();
    auto result = baseObject->createSampler(desc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outSampler, outObject);
    return result;
}

Result DebugDevice::createTextureView(ITexture* texture, IResourceView::Desc const& desc, IResourceView** outView)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugResourceView> outObject = new DebugResourceView();
    auto result = baseObject->createTextureView(getInnerObj(texture), desc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outView, outObject);
    return result;
}

Result DebugDevice::createBufferView(
    IBuffer* buffer,
    IBuffer* counterBuffer,
    IResourceView::Desc const& desc,
    IResourceView** outView
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugResourceView> outObject = new DebugResourceView();
    auto result =
        baseObject
            ->createBufferView(getInnerObj(buffer), getInnerObj(counterBuffer), desc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outView, outObject);
    return result;
}

Result DebugDevice::getAccelerationStructurePrebuildInfo(
    const IAccelerationStructure::BuildInputs& buildInputs,
    IAccelerationStructure::PrebuildInfo* outPrebuildInfo
)
{
    SLANG_RHI_API_FUNC;
    validateAccelerationStructureBuildInputs(buildInputs);
    return baseObject->getAccelerationStructurePrebuildInfo(buildInputs, outPrebuildInfo);
}

Result DebugDevice::createAccelerationStructure(
    const IAccelerationStructure::CreateDesc& desc,
    IAccelerationStructure** outAS
)
{
    SLANG_RHI_API_FUNC;
    auto innerDesc = desc;
    innerDesc.buffer = getInnerObj(innerDesc.buffer);
    RefPtr<DebugAccelerationStructure> outObject = new DebugAccelerationStructure();
    auto result = baseObject->createAccelerationStructure(innerDesc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outAS, outObject);
    return SLANG_OK;
}

Result DebugDevice::createFramebufferLayout(FramebufferLayoutDesc const& desc, IFramebufferLayout** outFrameBuffer)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugFramebufferLayout> outObject = new DebugFramebufferLayout();
    auto result = baseObject->createFramebufferLayout(desc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outFrameBuffer, outObject);
    return result;
}

Result DebugDevice::createSwapchain(ISwapchain::Desc const& desc, WindowHandle window, ISwapchain** outSwapchain)
{
    SLANG_RHI_API_FUNC;

    auto innerDesc = desc;
    innerDesc.queue = getInnerObj(desc.queue);
    RefPtr<DebugSwapchain> outObject = new DebugSwapchain();
    outObject->queue = getDebugObj(desc.queue);
    auto result = baseObject->createSwapchain(innerDesc, window, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outSwapchain, outObject);
    return Result();
}

Result DebugDevice::createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugInputLayout> outObject = new DebugInputLayout();
    auto result = baseObject->createInputLayout(desc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outLayout, outObject);
    return result;
}

Result DebugDevice::createCommandQueue(const ICommandQueue::Desc& desc, ICommandQueue** outQueue)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugCommandQueue> outObject = new DebugCommandQueue();
    auto result = baseObject->createCommandQueue(desc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outQueue, outObject);
    return result;
}

Result DebugDevice::createShaderObject(
    slang::TypeReflection* type,
    ShaderObjectContainerType containerType,
    IShaderObject** outShaderObject
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugShaderObject> outObject = new DebugShaderObject();
    auto result = baseObject->createShaderObject(type, containerType, outObject->baseObject.writeRef());
    outObject->m_typeName = string::from_cstr(type->getName());
    outObject->m_device = this;
    outObject->m_slangType = type;
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outShaderObject, outObject);
    return result;
}

Result DebugDevice::createShaderObject2(
    slang::ISession* session,
    slang::TypeReflection* type,
    ShaderObjectContainerType containerType,
    IShaderObject** outShaderObject
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugShaderObject> outObject = new DebugShaderObject();
    auto result = baseObject->createShaderObject2(session, type, containerType, outObject->baseObject.writeRef());
    outObject->m_typeName = string::from_cstr(type->getName());
    outObject->m_device = this;
    outObject->m_slangType = type;
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outShaderObject, outObject);
    return result;
}

Result DebugDevice::createMutableShaderObject(
    slang::TypeReflection* type,
    ShaderObjectContainerType containerType,
    IShaderObject** outShaderObject
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugShaderObject> outObject = new DebugShaderObject();
    auto result = baseObject->createMutableShaderObject(type, containerType, outObject->baseObject.writeRef());
    outObject->m_typeName = string::from_cstr(type->getName());
    outObject->m_device = this;
    outObject->m_slangType = type;
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outShaderObject, outObject);
    return result;
}

Result DebugDevice::createMutableShaderObject2(
    slang::ISession* session,
    slang::TypeReflection* type,
    ShaderObjectContainerType containerType,
    IShaderObject** outShaderObject
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugShaderObject> outObject = new DebugShaderObject();
    auto result =
        baseObject->createMutableShaderObject2(session, type, containerType, outObject->baseObject.writeRef());
    outObject->m_typeName = string::from_cstr(type->getName());
    outObject->m_device = this;
    outObject->m_slangType = type;
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outShaderObject, outObject);
    return result;
}

Result DebugDevice::createMutableRootShaderObject(IShaderProgram* program, IShaderObject** outRootObject)
{
    SLANG_RHI_API_FUNC;
    RefPtr<DebugShaderObject> outObject = new DebugShaderObject();
    auto result = baseObject->createMutableRootShaderObject(getInnerObj(program), outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    outObject->m_device = this;
    outObject->m_slangType = nullptr;
    outObject->m_rootComponentType = getDebugObj(program)->m_slangProgram;
    returnComPtr(outRootObject, outObject);
    return result;
}

Result DebugDevice::createShaderObjectFromTypeLayout(
    slang::TypeLayoutReflection* typeLayout,
    IShaderObject** outShaderObject
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugShaderObject> outObject = new DebugShaderObject();
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

Result DebugDevice::createMutableShaderObjectFromTypeLayout(
    slang::TypeLayoutReflection* typeLayout,
    IShaderObject** outShaderObject
)
{
    SLANG_RHI_API_FUNC;
    RefPtr<DebugShaderObject> outObject = new DebugShaderObject();
    auto result = baseObject->createMutableShaderObjectFromTypeLayout(typeLayout, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    auto type = typeLayout->getType();
    outObject->m_typeName = string::from_cstr(type->getName());
    outObject->m_device = this;
    outObject->m_slangType = type;
    returnComPtr(outShaderObject, outObject);
    return result;
}

Result DebugDevice::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnostics
)
{
    SLANG_RHI_API_FUNC;

    RefPtr<DebugShaderProgram> outObject = new DebugShaderProgram();
    auto result = baseObject->createShaderProgram(desc, outObject->baseObject.writeRef(), outDiagnostics);
    if (SLANG_FAILED(result))
        return result;
    outObject->m_slangProgram = desc.slangGlobalScope;
    returnComPtr(outProgram, outObject);
    return result;
}

Result DebugDevice::createRenderPipeline(const RenderPipelineDesc& desc, IPipeline** outPipeline)
{
    SLANG_RHI_API_FUNC;

    RenderPipelineDesc innerDesc = desc;
    innerDesc.program = getInnerObj(desc.program);
    innerDesc.inputLayout = getInnerObj(desc.inputLayout);
    innerDesc.framebufferLayout = getInnerObj(desc.framebufferLayout);
    RefPtr<DebugPipeline> outObject = new DebugPipeline();
    auto result = baseObject->createRenderPipeline(innerDesc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outPipeline, outObject);
    return result;
}

Result DebugDevice::createComputePipeline(const ComputePipelineDesc& desc, IPipeline** outPipeline)
{
    SLANG_RHI_API_FUNC;

    ComputePipelineDesc innerDesc = desc;
    innerDesc.program = getInnerObj(desc.program);

    RefPtr<DebugPipeline> outObject = new DebugPipeline();
    auto result = baseObject->createComputePipeline(innerDesc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outPipeline, outObject);
    return result;
}

Result DebugDevice::createRayTracingPipeline(const RayTracingPipelineDesc& desc, IPipeline** outPipeline)
{
    SLANG_RHI_API_FUNC;

    RayTracingPipelineDesc innerDesc = desc;
    innerDesc.program = getInnerObj(desc.program);

    RefPtr<DebugPipeline> outObject = new DebugPipeline();
    auto result = baseObject->createRayTracingPipeline(innerDesc, outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outPipeline, outObject);
    return result;
}

Result DebugDevice::readTexture(
    ITexture* resource,
    ResourceState state,
    ISlangBlob** outBlob,
    size_t* outRowPitch,
    size_t* outPixelSize
)
{
    SLANG_RHI_API_FUNC;
    return baseObject->readTexture(getInnerObj(resource), state, outBlob, outRowPitch, outPixelSize);
}

Result DebugDevice::readBuffer(IBuffer* buffer, size_t offset, size_t size, ISlangBlob** outBlob)
{
    SLANG_RHI_API_FUNC;
    return baseObject->readBuffer(getInnerObj(buffer), offset, size, outBlob);
}

const DeviceInfo& DebugDevice::getDeviceInfo() const
{
    SLANG_RHI_API_FUNC;
    return baseObject->getDeviceInfo();
}

Result DebugDevice::createQueryPool(const IQueryPool::Desc& desc, IQueryPool** outPool)
{
    SLANG_RHI_API_FUNC;
    RefPtr<DebugQueryPool> result = new DebugQueryPool();
    result->desc = desc;
    SLANG_RETURN_ON_FAIL(baseObject->createQueryPool(desc, result->baseObject.writeRef()));
    returnComPtr(outPool, result);
    return SLANG_OK;
}

Result DebugDevice::createFence(const IFence::Desc& desc, IFence** outFence)
{
    SLANG_RHI_API_FUNC;
    RefPtr<DebugFence> result = new DebugFence();
    SLANG_RETURN_ON_FAIL(baseObject->createFence(desc, result->baseObject.writeRef()));
    returnComPtr(outFence, result);
    return SLANG_OK;
}

Result DebugDevice::waitForFences(
    GfxCount fenceCount,
    IFence** fences,
    uint64_t* values,
    bool waitForAll,
    uint64_t timeout
)
{
    SLANG_RHI_API_FUNC;
    short_vector<IFence*> innerFences;
    for (GfxCount i = 0; i < fenceCount; i++)
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

Result DebugDevice::createShaderTable(const IShaderTable::Desc& desc, IShaderTable** outTable)
{
    SLANG_RHI_API_FUNC;
    RefPtr<DebugShaderTable> result = new DebugShaderTable();
    SLANG_RETURN_ON_FAIL(baseObject->createShaderTable(desc, result->baseObject.writeRef()));
    returnComPtr(outTable, result);
    return SLANG_OK;
}

} // namespace rhi::debug

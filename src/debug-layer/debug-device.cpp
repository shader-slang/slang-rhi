#include "debug-device.h"
#include "debug-command-queue.h"
#include "debug-fence.h"
#include "debug-heap.h"
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

Result DebugDevice::getFeatures(uint32_t* outFeatureCount, Feature* outFeatures)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getFeatures(outFeatureCount, outFeatures);
}

bool DebugDevice::hasFeature(Feature feature)
{
    SLANG_RHI_API_FUNC;

    return baseObject->hasFeature(feature);
}

bool DebugDevice::hasFeature(const char* feature)
{
    SLANG_RHI_API_FUNC;

    return baseObject->hasFeature(feature);
}

Result DebugDevice::getCapabilities(uint32_t* outCapabilityCount, Capability* outCapabilities)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getCapabilities(outCapabilityCount, outCapabilities);
}

bool DebugDevice::hasCapability(Capability capability)
{
    SLANG_RHI_API_FUNC;

    return baseObject->hasCapability(capability);
}

bool DebugDevice::hasCapability(const char* capability)
{
    SLANG_RHI_API_FUNC;

    return baseObject->hasCapability(capability);
}

Result DebugDevice::getFormatSupport(Format format, FormatSupport* outFormatSupport)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getFormatSupport(format, outFormatSupport);
}

DebugDevice::DebugDevice(DeviceType deviceType, IDebugCallback* debugCallback)
    : DebugObject(&m_ctx)
{
    ctx->deviceType = deviceType;
    ctx->debugCallback = debugCallback;
    SLANG_RHI_API_FUNC_NAME("CreateDevice");
    RHI_VALIDATION_INFO("Debug layer is enabled.");
}

Result DebugDevice::getSlangSession(slang::ISession** outSlangSession)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getSlangSession(outSlangSession);
}

Result DebugDevice::createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture)
{
    SLANG_RHI_API_FUNC;

    DeviceType deviceType = getDeviceType();

    if (uint32_t(desc.type) > uint32_t(TextureType::TextureCubeArray))
    {
        RHI_VALIDATION_ERROR("Invalid texture type");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.size.width < 1)
    {
        RHI_VALIDATION_ERROR("Texture width must be at least 1");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.size.height < 1)
    {
        RHI_VALIDATION_ERROR("Texture height must be at least 1");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.size.depth < 1)
    {
        RHI_VALIDATION_ERROR("Texture depth must be at least 1");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.arrayLength < 1)
    {
        RHI_VALIDATION_ERROR("Texture array length must be at least 1");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.mipCount < 1)
    {
        RHI_VALIDATION_ERROR("Texture mip count must be at least 1");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.format == Format::Undefined)
    {
        RHI_VALIDATION_ERROR("Texture format must be specified");
        return SLANG_E_INVALID_ARG;
    }

    if (desc.type != TextureType::Texture1DArray && desc.type != TextureType::Texture2DArray &&
        desc.type != TextureType::Texture2DMSArray && desc.type != TextureType::TextureCubeArray &&
        desc.arrayLength > 1)
    {
        RHI_VALIDATION_ERROR("Texture array length must be 1 for non-array textures");
        return SLANG_E_INVALID_ARG;
    }

    switch (desc.type)
    {
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
    {
        if (desc.sampleCount < 1)
        {
            RHI_VALIDATION_ERROR("Texture sample count must be at least 1");
            return SLANG_E_INVALID_ARG;
        }
        if (initData)
        {
            RHI_VALIDATION_ERROR("Texture with multisample type cannot have initial data");
            return SLANG_E_INVALID_ARG;
        }
        if (desc.mipCount != 1)
        {
            RHI_VALIDATION_ERROR("Texture with multisample type cannot have mip levels");
            return SLANG_E_INVALID_ARG;
        }
        if (deviceType == DeviceType::WGPU && desc.sampleCount != 4)
        {
            RHI_VALIDATION_ERROR("WebGPU only supports sample count of 4");
            return SLANG_E_INVALID_ARG;
        }
        if (deviceType == DeviceType::WGPU && desc.arrayLength != 1)
        {
            RHI_VALIDATION_ERROR("WebGPU doesn't support multisampled texture arrays");
            return SLANG_E_INVALID_ARG;
        }


        break;
    }
    default:
        break;
    }

    switch (desc.type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture1DArray:
        if (desc.size.height != 1 || desc.size.depth != 1)
        {
            RHI_VALIDATION_ERROR("1D textures must have height and depth set to 1");
            return SLANG_E_INVALID_ARG;
        }
        break;
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        if (desc.size.depth != 1)
        {
            RHI_VALIDATION_ERROR("2D textures must have depth set to 1");
            return SLANG_E_INVALID_ARG;
        }
        break;
    case TextureType::Texture3D:
        break;
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        if (desc.size.width != desc.size.height)
        {
            RHI_VALIDATION_ERROR("Cube textures must have width equal to height");
            return SLANG_E_INVALID_ARG;
        }
        if (desc.size.depth != 1)
        {
            RHI_VALIDATION_ERROR("Cube textures must have depth set to 1");
            return SLANG_E_INVALID_ARG;
        }
        break;
    }

    TextureDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createTextureLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }

    return baseObject->createTexture(patchedDesc, initData, outTexture);
}

Result DebugDevice::createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& desc, ITexture** outTexture)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createTextureFromNativeHandle(handle, desc, outTexture);
}

Result DebugDevice::createTextureFromSharedHandle(
    NativeHandle handle,
    const TextureDesc& desc,
    const size_t size,
    ITexture** outTexture
)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createTextureFromSharedHandle(handle, desc, size, outTexture);
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

Result DebugDevice::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createBufferFromNativeHandle(handle, desc, outBuffer);
}

Result DebugDevice::createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createBufferFromSharedHandle(handle, desc, outBuffer);
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

    if (desc.minFilter > TextureFilteringMode::Linear)
    {
        RHI_VALIDATION_ERROR("Invalid min filter mode");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.magFilter > TextureFilteringMode::Linear)
    {
        RHI_VALIDATION_ERROR("Invalid mag filter mode");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.mipFilter > TextureFilteringMode::Linear)
    {
        RHI_VALIDATION_ERROR("Invalid mip filter mode");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.reductionOp > TextureReductionOp::Maximum)
    {
        RHI_VALIDATION_ERROR("Invalid reduction op");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.addressU > TextureAddressingMode::MirrorOnce)
    {
        RHI_VALIDATION_ERROR("Invalid address U mode");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.addressV > TextureAddressingMode::MirrorOnce)
    {
        RHI_VALIDATION_ERROR("Invalid address V mode");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.addressW > TextureAddressingMode::MirrorOnce)
    {
        RHI_VALIDATION_ERROR("Invalid address W mode");
        return SLANG_E_INVALID_ARG;
    }
    if (ctx->deviceType == DeviceType::WGPU && (desc.addressU == TextureAddressingMode::ClampToBorder ||
                                                desc.addressV == TextureAddressingMode::ClampToBorder ||
                                                desc.addressW == TextureAddressingMode::ClampToBorder))
    {
        RHI_VALIDATION_ERROR("WebGPU doesn't support ClampToBorder mode");
        return SLANG_E_INVALID_ARG;
    }
    if (ctx->deviceType == DeviceType::WGPU &&
        (desc.addressU == TextureAddressingMode::MirrorOnce || desc.addressV == TextureAddressingMode::MirrorOnce ||
         desc.addressW == TextureAddressingMode::MirrorOnce))
    {
        RHI_VALIDATION_ERROR("WebGPU doesn't support MirrorOnce mode");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.comparisonFunc > ComparisonFunc::Always)
    {
        RHI_VALIDATION_ERROR("Invalid comparison func");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.maxAnisotropy > 1 &&
        (desc.minFilter == TextureFilteringMode::Point || desc.minFilter == TextureFilteringMode::Point))
    {
        RHI_VALIDATION_WARNING("maxAnisotropy > 1 can only be set when neither min and mag filter is Point");
    }

    if (desc.addressU == TextureAddressingMode::ClampToBorder ||
        desc.addressV == TextureAddressingMode::ClampToBorder || desc.addressW == TextureAddressingMode::ClampToBorder)
    {
        if (ctx->deviceType == DeviceType::WGPU)
        {
            RHI_VALIDATION_WARNING("WebGPU doesn't support ClampToBorder addressing mode");
        }

        const float* color = desc.borderColor;
        if (color[0] < 0.f || color[0] > 1.f || color[1] < 0.f || color[1] > 1.f || color[2] < 0.f || color[2] > 1.f ||
            color[3] < 0.f || color[3] > 1.f)
        {
            RHI_VALIDATION_ERROR("Invalid border color (must be in range [0, 1])");
            return SLANG_E_INVALID_ARG;
        }

        if (!(color[0] == 0.f && color[1] == 0.f && color[2] == 0.f && color[3] == 0.f) &&
            !(color[0] == 0.f && color[1] == 0.f && color[2] == 0.f && color[3] == 1.f) &&
            !(color[0] == 1.f && color[1] == 1.f && color[2] == 1.f && color[3] == 1.f) &&
            !baseObject->hasFeature(Feature::CustomBorderColor))
        {
            RHI_VALIDATION_WARNING(
                "Border color is not a predefined color and custom border color is not supported. "
                "Using transparent black instead."
            );
        }
    }

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
    SLANG_RETURN_ON_FAIL(validateAccelerationStructureBuildDesc(ctx, desc));
    return baseObject->getAccelerationStructureSizes(desc, outSizes);
}

Result DebugDevice::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    SLANG_RHI_API_FUNC;

    AccelerationStructureDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createAccelerationStructureLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }

    return baseObject->createAccelerationStructure(patchedDesc, outAccelerationStructure);
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

    ShaderProgramDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createShaderProgramLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }

    return baseObject->createShaderProgram(patchedDesc, outProgram, outDiagnostics);
}

Result DebugDevice::createRenderPipeline(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    SLANG_RHI_API_FUNC;

    if (desc.program == nullptr)
    {
        RHI_VALIDATION_ERROR("Program must be specified");
        return SLANG_E_INVALID_ARG;
    }
    if (ctx->deviceType == DeviceType::WGPU && desc.primitiveTopology == PrimitiveTopology::PatchList)
    {
        RHI_VALIDATION_ERROR("WebGPU doesn't support PatchList topology");
        return SLANG_E_INVALID_ARG;
    }
    if (ctx->deviceType == DeviceType::Metal && desc.primitiveTopology == PrimitiveTopology::PatchList)
    {
        RHI_VALIDATION_ERROR("Metal doesn't support PatchList topology");
        return SLANG_E_INVALID_ARG;
    }

    RenderPipelineDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createRenderPipelineLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }

    return baseObject->createRenderPipeline(patchedDesc, outPipeline);
}

Result DebugDevice::createComputePipeline(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    SLANG_RHI_API_FUNC;

    if (desc.program == nullptr)
    {
        RHI_VALIDATION_ERROR("Program must be specified");
        return SLANG_E_INVALID_ARG;
    }

    ComputePipelineDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createComputePipelineLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }

    return baseObject->createComputePipeline(patchedDesc, outPipeline);
}

Result DebugDevice::createRayTracingPipeline(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    SLANG_RHI_API_FUNC;

    if (desc.program == nullptr)
    {
        RHI_VALIDATION_ERROR("Program must be specified");
        return SLANG_E_INVALID_ARG;
    }

    RayTracingPipelineDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createRayTracingPipelineLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }

    return baseObject->createRayTracingPipeline(patchedDesc, outPipeline);
}

Result DebugDevice::getCompilationReportList(ISlangBlob** outReportListBlob)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getCompilationReportList(outReportListBlob);
}

Result DebugDevice::readTexture(
    ITexture* texture,
    uint32_t layer,
    uint32_t mip,
    const SubresourceLayout& layout,
    void* outData
)
{
    const TextureDesc& desc = texture->getDesc();

    if (layer > desc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Layer out of bounds");
        return SLANG_E_INVALID_ARG;
    }
    if (mip > desc.mipCount)
    {
        RHI_VALIDATION_ERROR("Mip out of bounds");
        return SLANG_E_INVALID_ARG;
    }

    switch (desc.type)
    {
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        RHI_VALIDATION_ERROR("Multisample textures cannot be read");
        return SLANG_E_INVALID_ARG;
    default:
        break;
    }

    SubresourceLayout expectedLayout;
    SLANG_RETURN_ON_FAIL(texture->getSubresourceLayout(mip, &expectedLayout));
    if (layout.size.width != expectedLayout.size.width || layout.size.height != expectedLayout.size.height ||
        layout.size.depth != expectedLayout.size.depth || layout.colPitch != expectedLayout.colPitch ||
        layout.rowPitch != expectedLayout.rowPitch || layout.slicePitch != expectedLayout.slicePitch ||
        layout.sizeInBytes != expectedLayout.sizeInBytes || layout.blockWidth != expectedLayout.blockWidth ||
        layout.blockHeight != expectedLayout.blockHeight || layout.rowCount != expectedLayout.rowCount)
    {
        RHI_VALIDATION_ERROR("Layout does not match the expected layout");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->readTexture(texture, layer, mip, layout, outData);
}

Result DebugDevice::readTexture(
    ITexture* texture,
    uint32_t layer,
    uint32_t mip,
    ISlangBlob** outBlob,
    SubresourceLayout* outLayout
)
{
    const TextureDesc& desc = texture->getDesc();

    if (layer > desc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Layer out of bounds");
        return SLANG_E_INVALID_ARG;
    }
    if (mip > desc.mipCount)
    {
        RHI_VALIDATION_ERROR("Mip out of bounds");
        return SLANG_E_INVALID_ARG;
    }

    switch (desc.type)
    {
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        RHI_VALIDATION_ERROR("Multisample textures cannot be read");
        return SLANG_E_INVALID_ARG;
    default:
        break;
    }

    return baseObject->readTexture(texture, layer, mip, outBlob, outLayout);
}

Result DebugDevice::readBuffer(IBuffer* buffer, Offset offset, Size size, void* outData)
{
    SLANG_RHI_API_FUNC;
    return baseObject->readBuffer(buffer, offset, size, outData);
}

Result DebugDevice::readBuffer(IBuffer* buffer, size_t offset, size_t size, ISlangBlob** outBlob)
{
    SLANG_RHI_API_FUNC;
    return baseObject->readBuffer(buffer, offset, size, outBlob);
}

const DeviceInfo& DebugDevice::getInfo() const
{
    SLANG_RHI_API_FUNC;
    return baseObject->getInfo();
}

Result DebugDevice::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    SLANG_RHI_API_FUNC;

    QueryPoolDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createQueryPoolLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }

    RefPtr<DebugQueryPool> result = new DebugQueryPool(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->createQueryPool(patchedDesc, result->baseObject.writeRef()));
    returnComPtr(outPool, result);
    return SLANG_OK;
}

Result DebugDevice::createFence(const FenceDesc& desc, IFence** outFence)
{
    SLANG_RHI_API_FUNC;

    FenceDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createFenceLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }

    RefPtr<DebugFence> result = new DebugFence(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->createFence(patchedDesc, result->baseObject.writeRef()));
    returnComPtr(outFence, result);
    return SLANG_OK;
}

Result DebugDevice::waitForFences(
    uint32_t fenceCount,
    IFence** fences,
    const uint64_t* fenceValues,
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
    return baseObject->waitForFences(fenceCount, innerFences.data(), fenceValues, waitForAll, timeout);
}

Result DebugDevice::createHeap(const HeapDesc& desc, IHeap** outHeap)
{
    HeapDesc patchedDesc = desc;
    std::string label;
    if (!patchedDesc.label)
    {
        label = createHeapLabel(patchedDesc);
        patchedDesc.label = label.c_str();
    }

    RefPtr<DebugHeap> result = new DebugHeap(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->createHeap(patchedDesc, result->baseObject.writeRef()));
    returnComPtr(outHeap, result);
    return SLANG_OK;
}

Result DebugDevice::getTextureAllocationInfo(const TextureDesc& desc, size_t* outSize, size_t* outAlignment)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getTextureAllocationInfo(desc, outSize, outAlignment);
}

Result DebugDevice::getTextureRowAlignment(Format format, size_t* outAlignment)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getTextureRowAlignment(format, outAlignment);
}

Result DebugDevice::getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertiesCount)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getCooperativeVectorProperties(properties, propertiesCount);
}

Result DebugDevice::getCooperativeVectorMatrixSize(
    uint32_t rowCount,
    uint32_t colCount,
    CooperativeVectorComponentType componentType,
    CooperativeVectorMatrixLayout layout,
    size_t rowColumnStride,
    size_t* outSize
)
{
    SLANG_RHI_API_FUNC;

    if (rowCount < 1 || rowCount > 128)
    {
        RHI_VALIDATION_ERROR("Row count must be in the range [1, 128]");
        return SLANG_E_INVALID_ARG;
    }
    if (colCount < 1 || colCount > 128)
    {
        RHI_VALIDATION_ERROR("Column count must be in the range [1, 128]");
        return SLANG_E_INVALID_ARG;
    }
    switch (layout)
    {
    case CooperativeVectorMatrixLayout::RowMajor:
    case CooperativeVectorMatrixLayout::ColumnMajor:
        break;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        if (rowColumnStride != 0)
        {
            RHI_VALIDATION_ERROR("Row/Column stride must be zero for optimal layouts");
            return SLANG_E_INVALID_ARG;
        }
        break;
    default:
        RHI_VALIDATION_ERROR("Invalid matrix layout");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject
        ->getCooperativeVectorMatrixSize(rowCount, colCount, componentType, layout, rowColumnStride, outSize);
}

Result DebugDevice::convertCooperativeVectorMatrix(
    void* dstBuffer,
    size_t dstBufferSize,
    const CooperativeVectorMatrixDesc* dstDescs,
    const void* srcBuffer,
    size_t srcBufferSize,
    const CooperativeVectorMatrixDesc* srcDescs,
    uint32_t matrixCount
)
{
    SLANG_RHI_API_FUNC;

    if (!dstBuffer)
    {
        RHI_VALIDATION_ERROR("Destination buffer must be valid");
        return SLANG_E_INVALID_ARG;
    }
    if (!srcBuffer)
    {
        RHI_VALIDATION_ERROR("Source buffer must be valid");
        return SLANG_E_INVALID_ARG;
    }

    SLANG_RETURN_ON_FAIL(
        validateConvertCooperativeVectorMatrix(ctx, dstBufferSize, dstDescs, srcBufferSize, srcDescs, matrixCount)
    );

    return baseObject->convertCooperativeVectorMatrix(
        dstBuffer,
        dstBufferSize,
        dstDescs,
        srcBuffer,
        srcBufferSize,
        srcDescs,
        matrixCount
    );
}

Result DebugDevice::createShaderTable(const ShaderTableDesc& desc, IShaderTable** outTable)
{
    SLANG_RHI_API_FUNC;

    return baseObject->createShaderTable(desc, outTable);
}

Result DebugDevice::reportHeaps(HeapReport* heapReports, uint32_t* heapCount)
{
    SLANG_RHI_API_FUNC;

    return baseObject->reportHeaps(heapReports, heapCount);
}

} // namespace rhi::debug

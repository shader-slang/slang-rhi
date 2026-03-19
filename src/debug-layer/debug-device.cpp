#include "debug-device.h"
#include "debug-command-queue.h"
#include "debug-fence.h"
#include "debug-heap.h"
#include "debug-helper-functions.h"
#include "debug-query.h"
#include "debug-shader-object.h"

#include "core/short_vector.h"
#include "resource-desc-utils.h"

#if SLANG_RHI_ENABLE_CUDA
#include <slang-rhi/cuda-driver-api.h>
#endif

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
    SLANG_RHI_DEBUG_API(IDevice, getNativeDeviceHandles);

    return baseObject->getNativeDeviceHandles(outHandles);
}

Result DebugDevice::getFeatures(uint32_t* outFeatureCount, Feature* outFeatures)
{
    SLANG_RHI_DEBUG_API(IDevice, getFeatures);

    return baseObject->getFeatures(outFeatureCount, outFeatures);
}

bool DebugDevice::hasFeature(Feature feature)
{
    SLANG_RHI_DEBUG_API(IDevice, hasFeature);

    return baseObject->hasFeature(feature);
}

bool DebugDevice::hasFeature(const char* feature)
{
    SLANG_RHI_DEBUG_API(IDevice, hasFeature);

    return baseObject->hasFeature(feature);
}

Result DebugDevice::getCapabilities(uint32_t* outCapabilityCount, Capability* outCapabilities)
{
    SLANG_RHI_DEBUG_API(IDevice, getCapabilities);

    return baseObject->getCapabilities(outCapabilityCount, outCapabilities);
}

bool DebugDevice::hasCapability(Capability capability)
{
    SLANG_RHI_DEBUG_API(IDevice, hasCapability);

    return baseObject->hasCapability(capability);
}

bool DebugDevice::hasCapability(const char* capability)
{
    SLANG_RHI_DEBUG_API(IDevice, hasCapability);

    return baseObject->hasCapability(capability);
}

Result DebugDevice::getFormatSupport(Format format, FormatSupport* outFormatSupport)
{
    SLANG_RHI_DEBUG_API(IDevice, getFormatSupport);

    return baseObject->getFormatSupport(format, outFormatSupport);
}

DebugDevice::DebugDevice(DeviceType deviceType, IDebugCallback* debugCallback)
    : DebugObject(&m_ctx)
{
    // Devices are created through IRHI::createDevice.
    SLANG_RHI_DEBUG_API(IRHI, createDevice);

    ctx->deviceType = deviceType;
    ctx->debugCallback = debugCallback;
    RHI_VALIDATION_INFO("Debug layer is enabled.");
}

Result DebugDevice::getSlangSession(slang::ISession** outSlangSession)
{
    SLANG_RHI_DEBUG_API(IDevice, getSlangSession);

    return baseObject->getSlangSession(outSlangSession);
}

Result DebugDevice::createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture)
{
    SLANG_RHI_DEBUG_API(IDevice, createTexture);

    validateCudaContext();

    if (!outTexture)
    {
        RHI_VALIDATION_ERROR("'outTexture' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidTextureType(desc.type))
    {
        RHI_VALIDATION_ERROR("Invalid texture type.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidFormat(desc.format))
    {
        RHI_VALIDATION_ERROR("Invalid texture format.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.format == Format::Undefined)
    {
        RHI_VALIDATION_ERROR("Texture format must be specified.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidTextureUsage(desc.usage))
    {
        RHI_VALIDATION_ERROR("Texture usage contains invalid flags.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.usage == TextureUsage::None)
    {
        RHI_VALIDATION_ERROR("Texture usage must be specified.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidMemoryType(desc.memoryType))
    {
        RHI_VALIDATION_ERROR("Invalid memory type.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.defaultState != ResourceState::Undefined && !isValidResourceState(desc.defaultState))
    {
        RHI_VALIDATION_ERROR("Invalid default resource state.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.size.width < 1)
    {
        RHI_VALIDATION_ERROR("Texture width must be at least 1.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.size.height < 1)
    {
        RHI_VALIDATION_ERROR("Texture height must be at least 1.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.size.depth < 1)
    {
        RHI_VALIDATION_ERROR("Texture depth must be at least 1.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.arrayLength < 1)
    {
        RHI_VALIDATION_ERROR("Texture array length must be at least 1.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.mipCount < 1 && desc.mipCount != kAllMips)
    {
        RHI_VALIDATION_ERROR("Texture mip count must be at least 1 (or kAllMips).");
        return SLANG_E_INVALID_ARG;
    }

    if (desc.type != TextureType::Texture1DArray && desc.type != TextureType::Texture2DArray &&
        desc.type != TextureType::Texture2DMSArray && desc.type != TextureType::TextureCubeArray &&
        desc.arrayLength > 1)
    {
        RHI_VALIDATION_ERROR("Texture array length must be 1 for non-array textures.");
        return SLANG_E_INVALID_ARG;
    }

    // Validate mip count does not exceed the maximum for the texture size.
    if (desc.mipCount != kAllMips)
    {
        uint32_t maxMips = calcMipCount(desc);
        if (maxMips > 0 && desc.mipCount > maxMips)
        {
            RHI_VALIDATION_ERROR_FORMAT(
                "Texture mip count (%d) exceeds maximum mip count (%d) for texture size.",
                desc.mipCount,
                maxMips
            );
            return SLANG_E_INVALID_ARG;
        }
    }

    // Validate mutually exclusive usage flags.
    if (is_set(desc.usage, TextureUsage::RenderTarget) && is_set(desc.usage, TextureUsage::DepthStencil))
    {
        RHI_VALIDATION_ERROR("Texture usage cannot have both RenderTarget and DepthStencil.");
        return SLANG_E_INVALID_ARG;
    }

    // Validate depth/stencil usage with compatible format.
    if (is_set(desc.usage, TextureUsage::DepthStencil) && !isDepthFormat(desc.format))
    {
        RHI_VALIDATION_ERROR("Texture with DepthStencil usage must use a depth format.");
        return SLANG_E_INVALID_ARG;
    }

    switch (desc.type)
    {
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
    {
        if (desc.sampleCount < 1)
        {
            RHI_VALIDATION_ERROR("Texture sample count must be at least 1.");
            return SLANG_E_INVALID_ARG;
        }
        if ((desc.sampleCount & (desc.sampleCount - 1)) != 0)
        {
            RHI_VALIDATION_ERROR("Texture sample count must be a power of 2.");
            return SLANG_E_INVALID_ARG;
        }
        if (initData)
        {
            RHI_VALIDATION_ERROR("Texture with multisample type cannot have initial data.");
            return SLANG_E_INVALID_ARG;
        }
        if (desc.mipCount != 1 && desc.mipCount != kAllMips)
        {
            RHI_VALIDATION_ERROR("Texture with multisample type cannot have multiple mip levels.");
            return SLANG_E_INVALID_ARG;
        }
        if (ctx->deviceType == DeviceType::WGPU && desc.sampleCount != 4)
        {
            RHI_VALIDATION_ERROR("WebGPU only supports sample count of 4.");
            return SLANG_E_INVALID_ARG;
        }
        if (ctx->deviceType == DeviceType::WGPU && desc.arrayLength != 1)
        {
            RHI_VALIDATION_ERROR("WebGPU does not support multisampled texture arrays.");
            return SLANG_E_INVALID_ARG;
        }

        break;
    }
    default:
        if (desc.sampleCount != 1)
        {
            RHI_VALIDATION_ERROR("Non-multisample texture types must have sample count of 1.");
            return SLANG_E_INVALID_ARG;
        }
        break;
    }

    switch (desc.type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture1DArray:
        if (desc.size.height != 1 || desc.size.depth != 1)
        {
            RHI_VALIDATION_ERROR("1D textures must have height and depth set to 1.");
            return SLANG_E_INVALID_ARG;
        }
        break;
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        if (desc.size.depth != 1)
        {
            RHI_VALIDATION_ERROR("2D textures must have depth set to 1.");
            return SLANG_E_INVALID_ARG;
        }
        break;
    case TextureType::Texture3D:
        break;
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        if (desc.size.width != desc.size.height)
        {
            RHI_VALIDATION_ERROR("Cube textures must have width equal to height.");
            return SLANG_E_INVALID_ARG;
        }
        if (desc.size.depth != 1)
        {
            RHI_VALIDATION_ERROR("Cube textures must have depth set to 1.");
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
    SLANG_RHI_DEBUG_API(IDevice, createTextureFromNativeHandle);

    if (!outTexture)
    {
        RHI_VALIDATION_ERROR("'outTexture' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->createTextureFromNativeHandle(handle, desc, outTexture);
}

Result DebugDevice::createTextureFromSharedHandle(
    NativeHandle handle,
    const TextureDesc& desc,
    const size_t size,
    ITexture** outTexture
)
{
    SLANG_RHI_DEBUG_API(IDevice, createTextureFromSharedHandle);

    if (!outTexture)
    {
        RHI_VALIDATION_ERROR("'outTexture' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->createTextureFromSharedHandle(handle, desc, size, outTexture);
}

Result DebugDevice::createBuffer(const BufferDesc& desc, const void* initData, IBuffer** outBuffer)
{
    SLANG_RHI_DEBUG_API(IDevice, createBuffer);

    validateCudaContext();

    if (!outBuffer)
    {
        RHI_VALIDATION_ERROR("'outBuffer' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.size == 0)
    {
        RHI_VALIDATION_ERROR("Buffer size must be greater than 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.usage == BufferUsage::None)
    {
        RHI_VALIDATION_ERROR("Buffer usage must be specified.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidBufferUsage(desc.usage))
    {
        RHI_VALIDATION_ERROR("Buffer usage contains invalid flags.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidMemoryType(desc.memoryType))
    {
        RHI_VALIDATION_ERROR("Invalid memory type.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.elementSize > 0 && desc.size % desc.elementSize != 0)
    {
        RHI_VALIDATION_WARNING("Buffer size is not a multiple of element size.");
    }

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
    SLANG_RHI_DEBUG_API(IDevice, createBufferFromNativeHandle);

    if (!outBuffer)
    {
        RHI_VALIDATION_ERROR("'outBuffer' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->createBufferFromNativeHandle(handle, desc, outBuffer);
}

Result DebugDevice::createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    SLANG_RHI_DEBUG_API(IDevice, createBufferFromSharedHandle);

    if (!outBuffer)
    {
        RHI_VALIDATION_ERROR("'outBuffer' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->createBufferFromSharedHandle(handle, desc, outBuffer);
}

Result DebugDevice::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    SLANG_RHI_DEBUG_API(IDevice, mapBuffer);

    if (!buffer)
    {
        RHI_VALIDATION_ERROR("'buffer' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (!outData)
    {
        RHI_VALIDATION_ERROR("'outData' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidCpuAccessMode(mode))
    {
        RHI_VALIDATION_ERROR("Invalid CpuAccessMode.");
        return SLANG_E_INVALID_ARG;
    }

    switch (mode)
    {
    case CpuAccessMode::Read:
        if (buffer->getDesc().memoryType != MemoryType::ReadBack)
        {
            RHI_VALIDATION_ERROR("Buffer must be created with MemoryType::ReadBack to map with CpuAccessMode::Read.");
            return SLANG_E_INVALID_ARG;
        }
        break;
    case CpuAccessMode::Write:
        if (buffer->getDesc().memoryType != MemoryType::Upload)
        {
            RHI_VALIDATION_ERROR("Buffer must be created with MemoryType::Upload to map with CpuAccessMode::Write.");
            return SLANG_E_INVALID_ARG;
        }
        break;
    default:
        break;
    }

#if SLANG_RHI_DEBUG_ENABLE_BUFFER_MAP_VALIDATION
    {
        std::lock_guard<std::mutex> lock(m_mappedBuffersMutex);
        if (m_mappedBuffers.count(buffer))
        {
            RHI_VALIDATION_ERROR("Buffer is already mapped.");
            return SLANG_E_INVALID_ARG;
        }
    }
#endif
    Result result = baseObject->mapBuffer(buffer, mode, outData);
#if SLANG_RHI_DEBUG_ENABLE_BUFFER_MAP_VALIDATION
    if (SLANG_SUCCEEDED(result))
    {
        std::lock_guard<std::mutex> lock(m_mappedBuffersMutex);
        m_mappedBuffers.insert(buffer);
    }
#endif

    return result;
}

Result DebugDevice::unmapBuffer(IBuffer* buffer)
{
    SLANG_RHI_DEBUG_API(IDevice, unmapBuffer);

    if (!buffer)
    {
        RHI_VALIDATION_ERROR("'buffer' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

#if SLANG_RHI_DEBUG_ENABLE_BUFFER_MAP_VALIDATION
    {
        std::lock_guard<std::mutex> lock(m_mappedBuffersMutex);
        if (!m_mappedBuffers.count(buffer))
        {
            RHI_VALIDATION_ERROR("Buffer is not mapped.");
            return SLANG_E_INVALID_ARG;
        }
    }
#endif
    Result result = baseObject->unmapBuffer(buffer);
#if SLANG_RHI_DEBUG_ENABLE_BUFFER_MAP_VALIDATION
    if (SLANG_SUCCEEDED(result))
    {
        std::lock_guard<std::mutex> lock(m_mappedBuffersMutex);
        m_mappedBuffers.erase(buffer);
    }
#endif

    return result;
}

Result DebugDevice::createSampler(const SamplerDesc& desc, ISampler** outSampler)
{
    SLANG_RHI_DEBUG_API(IDevice, createSampler);

    validateCudaContext();

    if (!outSampler)
    {
        RHI_VALIDATION_ERROR("'outSampler' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidTextureFilteringMode(desc.minFilter))
    {
        RHI_VALIDATION_ERROR("Invalid min filter mode.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidTextureFilteringMode(desc.magFilter))
    {
        RHI_VALIDATION_ERROR("Invalid mag filter mode.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidTextureFilteringMode(desc.mipFilter))
    {
        RHI_VALIDATION_ERROR("Invalid mip filter mode.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidTextureReductionOp(desc.reductionOp))
    {
        RHI_VALIDATION_ERROR("Invalid reduction op.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidTextureAddressingMode(desc.addressU))
    {
        RHI_VALIDATION_ERROR("Invalid address U mode.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidTextureAddressingMode(desc.addressV))
    {
        RHI_VALIDATION_ERROR("Invalid address V mode.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidTextureAddressingMode(desc.addressW))
    {
        RHI_VALIDATION_ERROR("Invalid address W mode.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidComparisonFunc(desc.comparisonFunc))
    {
        RHI_VALIDATION_ERROR("Invalid comparison func.");
        return SLANG_E_INVALID_ARG;
    }
    if (ctx->deviceType == DeviceType::WGPU && (desc.addressU == TextureAddressingMode::ClampToBorder ||
                                                desc.addressV == TextureAddressingMode::ClampToBorder ||
                                                desc.addressW == TextureAddressingMode::ClampToBorder))
    {
        RHI_VALIDATION_ERROR("WebGPU does not support ClampToBorder mode.");
        return SLANG_E_INVALID_ARG;
    }
    if (ctx->deviceType == DeviceType::WGPU &&
        (desc.addressU == TextureAddressingMode::MirrorOnce || desc.addressV == TextureAddressingMode::MirrorOnce ||
         desc.addressW == TextureAddressingMode::MirrorOnce))
    {
        RHI_VALIDATION_ERROR("WebGPU does not support MirrorOnce mode.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.maxAnisotropy == 0)
    {
        RHI_VALIDATION_ERROR("maxAnisotropy must be at least 1.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.maxAnisotropy > 16)
    {
        RHI_VALIDATION_ERROR("maxAnisotropy exceeds maximum supported value of 16.");
        return SLANG_E_INVALID_ARG;
    }
    // Anisotropic filtering replaces both min and mag filters, so both must be linear.
    // This is required by D3D12 and Vulkan specs.
    if (desc.maxAnisotropy > 1 &&
        (desc.minFilter == TextureFilteringMode::Point || desc.magFilter == TextureFilteringMode::Point))
    {
        RHI_VALIDATION_ERROR("maxAnisotropy > 1 requires Linear min and mag filters.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.minLOD > desc.maxLOD)
    {
        RHI_VALIDATION_ERROR("minLOD must not be greater than maxLOD.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.mipLODBias < -16.f || desc.mipLODBias > 15.99f)
    {
        RHI_VALIDATION_ERROR("mipLODBias is outside the supported range [-16, 15.99].");
        return SLANG_E_INVALID_ARG;
    }

    if (desc.addressU == TextureAddressingMode::ClampToBorder ||
        desc.addressV == TextureAddressingMode::ClampToBorder || desc.addressW == TextureAddressingMode::ClampToBorder)
    {
        const float* color = desc.borderColor;
        if (color[0] < 0.f || color[0] > 1.f || color[1] < 0.f || color[1] > 1.f || color[2] < 0.f || color[2] > 1.f ||
            color[3] < 0.f || color[3] > 1.f)
        {
            RHI_VALIDATION_ERROR("Invalid border color (must be in range [0, 1]).");
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
    SLANG_RHI_DEBUG_API(IDevice, createTextureView);

    validateCudaContext();

    if (!outView)
    {
        RHI_VALIDATION_ERROR("'outView' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (!texture)
    {
        RHI_VALIDATION_ERROR("'texture' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidTextureAspect(desc.aspect))
    {
        RHI_VALIDATION_ERROR("Invalid texture aspect.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.format != Format::Undefined && !isValidFormat(desc.format))
    {
        RHI_VALIDATION_ERROR("Invalid format.");
        return SLANG_E_INVALID_ARG;
    }
    if (!validateSubresourceRange(desc.subresourceRange, texture->getDesc()))
    {
        RHI_VALIDATION_ERROR("Subresource range is out of bounds for the texture.");
        return SLANG_E_INVALID_ARG;
    }

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
    SLANG_RHI_DEBUG_API(IDevice, getAccelerationStructureSizes);

    SLANG_RETURN_ON_FAIL(validateAccelerationStructureBuildDesc(ctx, desc));

    return baseObject->getAccelerationStructureSizes(desc, outSizes);
}

Result DebugDevice::getClusterOperationSizes(const ClusterOperationParams& params, ClusterOperationSizes* outSizes)
{
    SLANG_RHI_DEBUG_API(IDevice, getClusterOperationSizes);

    SLANG_RETURN_ON_FAIL(validateClusterOperationParams(ctx, params));

    return baseObject->getClusterOperationSizes(params, outSizes);
}

Result DebugDevice::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    SLANG_RHI_DEBUG_API(IDevice, createAccelerationStructure);

    validateCudaContext();

    if (!outAccelerationStructure)
    {
        RHI_VALIDATION_ERROR("'outAccelerationStructure' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.size == 0)
    {
        RHI_VALIDATION_ERROR("Acceleration structure size must be greater than 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidAccelerationStructureBuildFlags(desc.flags))
    {
        RHI_VALIDATION_ERROR("Acceleration structure build flags contain invalid flags.");
        return SLANG_E_INVALID_ARG;
    }
    if (is_set(desc.flags, AccelerationStructureBuildFlags::CreateMotion) &&
        !baseObject->hasFeature(Feature::RayTracingMotionBlur))
    {
        RHI_VALIDATION_ERROR("Acceleration structure with CreateMotion flag requires RayTracingMotionBlur feature.");
        return SLANG_E_INVALID_ARG;
    }
    if (is_set(desc.flags, AccelerationStructureBuildFlags::CreateMotion) && desc.motionInfo.enabled &&
        desc.motionInfo.maxInstances == 0)
    {
        RHI_VALIDATION_ERROR("Motion-enabled acceleration structure requires maxInstances > 0.");
        return SLANG_E_INVALID_ARG;
    }

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
    SLANG_RHI_DEBUG_API(IDevice, createSurface);

    RefPtr<DebugSurface> outObject = new DebugSurface(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->createSurface(windowHandle, outObject->baseObject.writeRef()));

    returnComPtr(outSurface, outObject);
    return SLANG_OK;
}

Result DebugDevice::createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout)
{
    SLANG_RHI_DEBUG_API(IDevice, createInputLayout);

    if (!outLayout)
    {
        RHI_VALIDATION_ERROR("'outLayout' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.inputElementCount > 0 && desc.inputElements == nullptr)
    {
        RHI_VALIDATION_ERROR("'inputElements' is null but 'inputElementCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.vertexStreamCount > 0 && desc.vertexStreams == nullptr)
    {
        RHI_VALIDATION_ERROR("'vertexStreams' is null but 'vertexStreamCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }
    for (uint32_t i = 0; i < desc.inputElementCount; ++i)
    {
        const InputElementDesc& element = desc.inputElements[i];
        if (element.bufferSlotIndex >= desc.vertexStreamCount)
        {
            RHI_VALIDATION_ERROR_FORMAT(
                "Input element %u 'bufferSlotIndex' (%u) is out of range ('vertexStreamCount' = %u).",
                i,
                element.bufferSlotIndex,
                desc.vertexStreamCount
            );
            return SLANG_E_INVALID_ARG;
        }
        if (!isValidFormat(element.format))
        {
            RHI_VALIDATION_ERROR_FORMAT("Input element %u has invalid format.", i);
            return SLANG_E_INVALID_ARG;
        }
        if (element.format == Format::Undefined)
        {
            RHI_VALIDATION_ERROR_FORMAT("Input element %u format must be specified.", i);
            return SLANG_E_INVALID_ARG;
        }
    }

    return baseObject->createInputLayout(desc, outLayout);
}

Result DebugDevice::getQueue(QueueType type, ICommandQueue** outQueue)
{
    SLANG_RHI_DEBUG_API(IDevice, getQueue);

    RefPtr<DebugCommandQueue> outObject = new DebugCommandQueue(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->getQueue(type, outObject->baseObject.writeRef()));

    returnComPtr(outQueue, outObject);
    return SLANG_OK;
}

Result DebugDevice::createShaderObject(
    slang::ISession* session,
    slang::TypeReflection* type,
    ShaderObjectContainerType containerType,
    IShaderObject** outShaderObject
)
{
    SLANG_RHI_DEBUG_API(IDevice, createShaderObject);

    RefPtr<DebugShaderObject> outObject = new DebugShaderObject(ctx);
    SLANG_RETURN_ON_FAIL(
        baseObject->createShaderObject(session, type, containerType, outObject->baseObject.writeRef())
    );
    outObject->m_typeName = string::from_cstr(type->getName());
    outObject->m_device = this;
    outObject->m_slangType = type;

    returnComPtr(outShaderObject, outObject);
    return SLANG_OK;
}

Result DebugDevice::createShaderObjectFromTypeLayout(
    slang::TypeLayoutReflection* typeLayout,
    IShaderObject** outShaderObject
)
{
    SLANG_RHI_DEBUG_API(IDevice, createShaderObjectFromTypeLayout);

    RefPtr<DebugShaderObject> outObject = new DebugShaderObject(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->createShaderObjectFromTypeLayout(typeLayout, outObject->baseObject.writeRef()));
    auto type = typeLayout->getType();
    outObject->m_typeName = string::from_cstr(type->getName());
    outObject->m_device = this;
    outObject->m_slangType = type;

    returnComPtr(outShaderObject, outObject);
    return SLANG_OK;
}

Result DebugDevice::createRootShaderObject(IShaderProgram* program, IShaderObject** outObject)
{
    SLANG_RHI_DEBUG_API(IDevice, createRootShaderObject);

    RefPtr<DebugShaderObject> outRootObject = new DebugShaderObject(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->createRootShaderObject(program, outRootObject->baseObject.writeRef()));

    returnComPtr(outObject, outRootObject);
    return SLANG_OK;
}

Result DebugDevice::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnostics
)
{
    SLANG_RHI_DEBUG_API(IDevice, createShaderProgram);

    validateCudaContext();

    if (!outProgram)
    {
        RHI_VALIDATION_ERROR("'outProgram' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.slangGlobalScope == nullptr && desc.slangEntryPointCount == 0)
    {
        RHI_VALIDATION_ERROR("Shader program requires at least a global scope or entry point.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.slangEntryPointCount > 0 && desc.slangEntryPoints == nullptr)
    {
        RHI_VALIDATION_ERROR("'slangEntryPoints' is null but 'slangEntryPointCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }

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
    SLANG_RHI_DEBUG_API(IDevice, createRenderPipeline);

    validateCudaContext();

    if (desc.program == nullptr)
    {
        RHI_VALIDATION_ERROR("Program must be specified.");
        return SLANG_E_INVALID_ARG;
    }
    if (ctx->deviceType == DeviceType::WGPU && desc.primitiveTopology == PrimitiveTopology::PatchList)
    {
        RHI_VALIDATION_ERROR("WebGPU does not support PatchList topology.");
        return SLANG_E_INVALID_ARG;
    }
    if (ctx->deviceType == DeviceType::Metal && desc.primitiveTopology == PrimitiveTopology::PatchList)
    {
        RHI_VALIDATION_ERROR("Metal does not support PatchList topology.");
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
    SLANG_RHI_DEBUG_API(IDevice, createComputePipeline);

    validateCudaContext();

    if (desc.program == nullptr)
    {
        RHI_VALIDATION_ERROR("Program must be specified.");
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
    SLANG_RHI_DEBUG_API(IDevice, createRayTracingPipeline);

    validateCudaContext();

    if (desc.program == nullptr)
    {
        RHI_VALIDATION_ERROR("Program must be specified.");
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
    SLANG_RHI_DEBUG_API(IDevice, getCompilationReportList);

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
    SLANG_RHI_DEBUG_API(IDevice, readTexture);

    validateCudaContext();

    const TextureDesc& desc = texture->getDesc();

    if (layer >= desc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Layer out of bounds.");
        return SLANG_E_INVALID_ARG;
    }
    if (mip >= desc.mipCount)
    {
        RHI_VALIDATION_ERROR("Mip out of bounds.");
        return SLANG_E_INVALID_ARG;
    }

    switch (desc.type)
    {
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        RHI_VALIDATION_ERROR("Multisample textures cannot be read.");
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
        RHI_VALIDATION_ERROR("Layout does not match the expected layout.");
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
    SLANG_RHI_DEBUG_API(IDevice, readTexture);

    validateCudaContext();

    const TextureDesc& desc = texture->getDesc();

    if (layer >= desc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Layer out of bounds.");
        return SLANG_E_INVALID_ARG;
    }
    if (mip >= desc.mipCount)
    {
        RHI_VALIDATION_ERROR("Mip out of bounds.");
        return SLANG_E_INVALID_ARG;
    }

    switch (desc.type)
    {
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        RHI_VALIDATION_ERROR("Multisample textures cannot be read.");
        return SLANG_E_INVALID_ARG;
    default:
        break;
    }

    return baseObject->readTexture(texture, layer, mip, outBlob, outLayout);
}

Result DebugDevice::readBuffer(IBuffer* buffer, Offset offset, Size size, void* outData)
{
    SLANG_RHI_DEBUG_API(IDevice, readBuffer);

    validateCudaContext();

    if (!buffer)
    {
        RHI_VALIDATION_ERROR("'buffer' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (!outData)
    {
        RHI_VALIDATION_ERROR("'outData' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (size == 0)
    {
        RHI_VALIDATION_ERROR("Read size must be greater than 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (offset + size > buffer->getDesc().size)
    {
        RHI_VALIDATION_ERROR("Read range (offset + size) exceeds buffer size.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->readBuffer(buffer, offset, size, outData);
}

Result DebugDevice::readBuffer(IBuffer* buffer, size_t offset, size_t size, ISlangBlob** outBlob)
{
    SLANG_RHI_DEBUG_API(IDevice, readBuffer);

    validateCudaContext();

    if (!buffer)
    {
        RHI_VALIDATION_ERROR("'buffer' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (!outBlob)
    {
        RHI_VALIDATION_ERROR("'outBlob' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (size == 0)
    {
        RHI_VALIDATION_ERROR("Read size must be greater than 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (offset + size > buffer->getDesc().size)
    {
        RHI_VALIDATION_ERROR("Read range (offset + size) exceeds buffer size.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->readBuffer(buffer, offset, size, outBlob);
}

const DeviceInfo& DebugDevice::getInfo() const
{
    SLANG_RHI_DEBUG_API(IDevice, getInfo);

    return baseObject->getInfo();
}

Result DebugDevice::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    SLANG_RHI_DEBUG_API(IDevice, createQueryPool);

    validateCudaContext();

    if (!outPool)
    {
        RHI_VALIDATION_ERROR("'outPool' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.count == 0)
    {
        RHI_VALIDATION_ERROR("Query pool count must be greater than 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidQueryType(desc.type))
    {
        RHI_VALIDATION_ERROR("Invalid query type.");
        return SLANG_E_INVALID_ARG;
    }

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
    SLANG_RHI_DEBUG_API(IDevice, createFence);

    validateCudaContext();

    if (!outFence)
    {
        RHI_VALIDATION_ERROR("'outFence' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

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
    SLANG_RHI_DEBUG_API(IDevice, waitForFences);

    short_vector<IFence*> innerFences;
    for (uint32_t i = 0; i < fenceCount; i++)
    {
        innerFences.push_back(getInnerObj(fences[i]));
    }

    return baseObject->waitForFences(fenceCount, innerFences.data(), fenceValues, waitForAll, timeout);
}

Result DebugDevice::createHeap(const HeapDesc& desc, IHeap** outHeap)
{
    SLANG_RHI_DEBUG_API(IDevice, createHeap);

    validateCudaContext();

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
    SLANG_RHI_DEBUG_API(IDevice, getTextureAllocationInfo);

    return baseObject->getTextureAllocationInfo(desc, outSize, outAlignment);
}

Result DebugDevice::getTextureRowAlignment(Format format, size_t* outAlignment)
{
    SLANG_RHI_DEBUG_API(IDevice, getTextureRowAlignment);

    return baseObject->getTextureRowAlignment(format, outAlignment);
}

Result DebugDevice::isCooperativeMatrixSupported(const CooperativeMatrixDesc& desc, bool* outSupported)
{
    SLANG_RHI_DEBUG_API(IDevice, isCooperativeMatrixSupported);

    return baseObject->isCooperativeMatrixSupported(desc, outSupported);
}

Result DebugDevice::getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertiesCount)
{
    SLANG_RHI_DEBUG_API(IDevice, getCooperativeVectorProperties);

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
    SLANG_RHI_DEBUG_API(IDevice, getCooperativeVectorMatrixSize);

    if (rowCount < 1 || rowCount > 128)
    {
        RHI_VALIDATION_ERROR("Row count must be in the range [1, 128].");
        return SLANG_E_INVALID_ARG;
    }
    if (colCount < 1 || colCount > 128)
    {
        RHI_VALIDATION_ERROR("Column count must be in the range [1, 128].");
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
            RHI_VALIDATION_ERROR("Row/Column stride must be zero for optimal layouts.");
            return SLANG_E_INVALID_ARG;
        }
        break;
    default:
        RHI_VALIDATION_ERROR("Invalid matrix layout.");
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
    SLANG_RHI_DEBUG_API(IDevice, convertCooperativeVectorMatrix);

    if (!dstBuffer)
    {
        RHI_VALIDATION_ERROR("'dstBuffer' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (!srcBuffer)
    {
        RHI_VALIDATION_ERROR("'srcBuffer' must not be null.");
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
    SLANG_RHI_DEBUG_API(IDevice, createShaderTable);

    if (!outTable)
    {
        RHI_VALIDATION_ERROR("'outTable' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.program == nullptr)
    {
        RHI_VALIDATION_ERROR("'program' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.rayGenShaderCount > 0 && desc.rayGenShaderEntryPointNames == nullptr)
    {
        RHI_VALIDATION_ERROR("'rayGenShaderEntryPointNames' is null but 'rayGenShaderCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.missShaderCount > 0 && desc.missShaderEntryPointNames == nullptr)
    {
        RHI_VALIDATION_ERROR("'missShaderEntryPointNames' is null but 'missShaderCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.hitGroupCount > 0 && desc.hitGroupNames == nullptr)
    {
        RHI_VALIDATION_ERROR("'hitGroupNames' is null but 'hitGroupCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.callableShaderCount > 0 && desc.callableShaderEntryPointNames == nullptr)
    {
        RHI_VALIDATION_ERROR("'callableShaderEntryPointNames' is null but 'callableShaderCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->createShaderTable(desc, outTable);
}

Result DebugDevice::reportHeaps(HeapReport* heapReports, uint32_t* heapCount)
{
    SLANG_RHI_DEBUG_API(IDevice, reportHeaps);

    return baseObject->reportHeaps(heapReports, heapCount);
}

Result DebugDevice::setCudaContextCurrent()
{
    SLANG_RHI_DEBUG_API(IDevice, setCudaContextCurrent);

    return baseObject->setCudaContextCurrent();
}

Result DebugDevice::pushCudaContext()
{
    SLANG_RHI_DEBUG_API(IDevice, pushCudaContext);

    return baseObject->pushCudaContext();
}

Result DebugDevice::popCudaContext()
{
    SLANG_RHI_DEBUG_API(IDevice, popCudaContext);

    return baseObject->popCudaContext();
}

void DebugDevice::validateCudaContext()
{
#if SLANG_RHI_ENABLE_CUDA
    if (ctx->deviceType != DeviceType::CUDA)
        return;

    // Get the expected context from the device.
    DeviceNativeHandles handles;
    baseObject->getNativeDeviceHandles(&handles);
    CUcontext expectedContext = nullptr;
    for (const auto& handle : handles.handles)
    {
        if (handle.type == NativeHandleType::CUcontext)
        {
            expectedContext = (CUcontext)handle.value;
            break;
        }
    }
    if (!expectedContext)
        return;

    // Check the current context.
    CUcontext currentContext = nullptr;
    cuCtxGetCurrent(&currentContext);
    if (currentContext == nullptr)
    {
        RHI_VALIDATION_WARNING(
            "No CUDA context is current. Use setCudaContextCurrent() or SLANG_DEVICE_SCOPE to set the device context."
        );
    }
    else if (currentContext != expectedContext)
    {
        RHI_VALIDATION_WARNING(
            "Wrong CUDA context is current. Use setCudaContextCurrent() or SLANG_DEVICE_SCOPE to set the correct "
            "device context."
        );
    }
#endif
}

} // namespace rhi::debug

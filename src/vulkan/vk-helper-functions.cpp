#include "vk-helper-functions.h"
#include "vk-device.h"
#include "vk-util.h"

#include <vector>

namespace rhi::vk {

Size calcRowSize(Format format, uint32_t width)
{
    const FormatInfo& info = getFormatInfo(format);
    return Size((width + info.blockWidth - 1) / info.blockWidth * info.blockSizeInBytes);
}

uint32_t calcNumRows(Format format, uint32_t height)
{
    const FormatInfo& info = getFormatInfo(format);
    return (height + info.blockHeight - 1) / info.blockHeight;
}

VkAttachmentLoadOp translateLoadOp(LoadOp loadOp)
{
    switch (loadOp)
    {
    case LoadOp::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    default:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
}

VkAttachmentStoreOp translateStoreOp(StoreOp storeOp)
{
    switch (storeOp)
    {
    case StoreOp::Store:
        return VK_ATTACHMENT_STORE_OP_STORE;
    default:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
}

VkPipelineCreateFlags translateRayTracingPipelineFlags(RayTracingPipelineFlags flags)
{
    VkPipelineCreateFlags vkFlags = 0;
    if (is_set(flags, RayTracingPipelineFlags::SkipTriangles))
        vkFlags |= VK_PIPELINE_CREATE_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR;
    if (is_set(flags, RayTracingPipelineFlags::SkipProcedurals))
        vkFlags |= VK_PIPELINE_CREATE_RAY_TRACING_SKIP_AABBS_BIT_KHR;

    return vkFlags;
}

uint32_t getMipLevelSize(uint32_t mipLevel, uint32_t size)
{
    return max(1u, (size >> mipLevel));
}

VkImageLayout translateImageLayout(ResourceState state)
{
    switch (state)
    {
    case ResourceState::Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case ResourceState::UnorderedAccess:
        return VK_IMAGE_LAYOUT_GENERAL;
    case ResourceState::RenderTarget:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthRead:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case ResourceState::DepthWrite:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ResourceState::ShaderResource:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ResourceState::ResolveDestination:
    case ResourceState::CopyDestination:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case ResourceState::ResolveSource:
    case ResourceState::CopySource:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ResourceState::Present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported");
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

VkAccessFlagBits calcAccessFlags(ResourceState state)
{
    switch (state)
    {
    case ResourceState::Undefined:
    case ResourceState::Present:
        return VkAccessFlagBits(0);
    case ResourceState::VertexBuffer:
        return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    case ResourceState::ConstantBuffer:
        return VK_ACCESS_UNIFORM_READ_BIT;
    case ResourceState::IndexBuffer:
        return VK_ACCESS_INDEX_READ_BIT;
    case ResourceState::RenderTarget:
        return VkAccessFlagBits(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
    case ResourceState::ShaderResource:
        return VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    case ResourceState::UnorderedAccess:
        return VkAccessFlagBits(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    case ResourceState::DepthRead:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    case ResourceState::DepthWrite:
        return VkAccessFlagBits(
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        );
    case ResourceState::IndirectArgument:
        return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    case ResourceState::ResolveDestination:
    case ResourceState::CopyDestination:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
    case ResourceState::ResolveSource:
    case ResourceState::CopySource:
        return VK_ACCESS_TRANSFER_READ_BIT;
    case ResourceState::AccelerationStructure:
        return VkAccessFlagBits(
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
        );
    case ResourceState::AccelerationStructureBuildInput:
        return VkAccessFlagBits(VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    case ResourceState::General:
        return VkAccessFlagBits(VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported");
        return VkAccessFlagBits(0);
    }
}

VkPipelineStageFlagBits calcPipelineStageFlags(ResourceState state, bool src)
{
    switch (state)
    {
    case ResourceState::Undefined:
        SLANG_RHI_ASSERT(src);
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    case ResourceState::VertexBuffer:
    case ResourceState::IndexBuffer:
        return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    case ResourceState::ConstantBuffer:
    case ResourceState::UnorderedAccess:
        return VkPipelineStageFlagBits(
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
            VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
        );
    case ResourceState::ShaderResource:
        return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    case ResourceState::RenderTarget:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case ResourceState::DepthRead:
    case ResourceState::DepthWrite:
        return VkPipelineStageFlagBits(
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
        );
    case ResourceState::IndirectArgument:
        return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    case ResourceState::CopySource:
    case ResourceState::CopyDestination:
    case ResourceState::ResolveSource:
    case ResourceState::ResolveDestination:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case ResourceState::Present:
        return src ? VkPipelineStageFlagBits(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
                   : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    case ResourceState::General:
        return VkPipelineStageFlagBits(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    case ResourceState::AccelerationStructure:
        return VkPipelineStageFlagBits(
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
            VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
        );
    case ResourceState::AccelerationStructureBuildInput:
        return VkPipelineStageFlagBits(VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported");
        return VkPipelineStageFlagBits(0);
    }
}

VkAccessFlags translateAccelerationStructureAccessFlag(AccessFlag access)
{
    VkAccessFlags result = 0;
    if ((uint32_t)access & (uint32_t)AccessFlag::Read)
        result |=
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    if ((uint32_t)access & (uint32_t)AccessFlag::Write)
        result |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    return result;
}

VkBufferUsageFlagBits _calcBufferUsageFlags(BufferUsage usage)
{
    int flags = 0;
    if (is_set(usage, BufferUsage::VertexBuffer))
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (is_set(usage, BufferUsage::IndexBuffer))
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (is_set(usage, BufferUsage::ConstantBuffer))
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (is_set(usage, BufferUsage::ShaderResource))
        flags |= (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (is_set(usage, BufferUsage::UnorderedAccess))
        flags |= (VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (is_set(usage, BufferUsage::IndirectArgument))
        flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (is_set(usage, BufferUsage::CopySource))
        flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (is_set(usage, BufferUsage::CopyDestination))
        flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (is_set(usage, BufferUsage::AccelerationStructure))
        flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    if (is_set(usage, BufferUsage::AccelerationStructureBuildInput))
        flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (is_set(usage, BufferUsage::ShaderTable))
        flags |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
    return VkBufferUsageFlagBits(flags);
}

VkImageUsageFlagBits _calcImageUsageFlags(ResourceState state)
{
    switch (state)
    {
    case ResourceState::RenderTarget:
        return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    case ResourceState::DepthWrite:
        return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    case ResourceState::DepthRead:
        return VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    case ResourceState::ShaderResource:
        return VK_IMAGE_USAGE_SAMPLED_BIT;
    case ResourceState::UnorderedAccess:
        return VK_IMAGE_USAGE_STORAGE_BIT;
    case ResourceState::CopySource:
        return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    case ResourceState::CopyDestination:
        return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    case ResourceState::ResolveSource:
        return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    case ResourceState::ResolveDestination:
        return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    case ResourceState::Present:
        return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    case ResourceState::Undefined:
    case ResourceState::General:
        return (VkImageUsageFlagBits)0;
    default:
    {
        SLANG_RHI_ASSERT_FAILURE("Unsupported");
        return VkImageUsageFlagBits(0);
    }
    }
}

VkImageViewType _calcImageViewType(TextureType type, const TextureDesc& desc)
{
    switch (type)
    {
    case TextureType::Texture1D:
        return desc.arrayLength > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
    case TextureType::Texture2D:
        return desc.arrayLength > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    case TextureType::TextureCube:
        return desc.arrayLength > 1 ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
    case TextureType::Texture3D:
    {
        // Can't have an array and 3d texture
        SLANG_RHI_ASSERT(desc.arrayLength <= 1);
        return VK_IMAGE_VIEW_TYPE_3D;
    }
    default:
        break;
    }

    return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
}

VkImageUsageFlagBits _calcImageUsageFlags(TextureUsage usage)
{
    int flags = 0;
    if (is_set(usage, TextureUsage::ShaderResource))
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (is_set(usage, TextureUsage::UnorderedAccess))
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (is_set(usage, TextureUsage::RenderTarget))
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (is_set(usage, TextureUsage::DepthRead))
        flags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    if (is_set(usage, TextureUsage::DepthWrite))
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (is_set(usage, TextureUsage::Present))
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (is_set(usage, TextureUsage::CopySource))
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (is_set(usage, TextureUsage::CopyDestination))
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (is_set(usage, TextureUsage::ResolveSource))
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (is_set(usage, TextureUsage::ResolveDestination))
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return VkImageUsageFlagBits(flags);
}

VkImageUsageFlags _calcImageUsageFlags(TextureUsage usage, MemoryType memoryType, const void* initData)
{
    VkImageUsageFlags flags = _calcImageUsageFlags(usage);

    if (memoryType == MemoryType::Upload || initData)
    {
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    return flags;
}

VkAccessFlags calcAccessFlagsFromImageLayout(VkImageLayout layout)
{
    switch (layout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
    case VK_IMAGE_LAYOUT_GENERAL:
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
        return (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_ACCESS_SHADER_READ_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return VK_ACCESS_TRANSFER_READ_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported VkImageLayout");
        return (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    }
}

VkPipelineStageFlags calcPipelineStageFlagsFromImageLayout(VkImageLayout layout)
{
    switch (layout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
    case VK_IMAGE_LAYOUT_GENERAL:
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return (VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
        return (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported VkImageLayout");
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}

VkImageAspectFlags getAspectMaskFromFormat(VkFormat format, TextureAspect aspect)
{
    switch (aspect)
    {
    case TextureAspect::All:
        switch (format)
        {
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    case TextureAspect::DepthOnly:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case TextureAspect::StencilOnly:
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

AdapterLUID getAdapterLUID(VulkanApi api, VkPhysicalDevice physicalDevice)
{
    AdapterLUID luid = {};

    VkPhysicalDeviceIDPropertiesKHR idProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext = &idProps;
    SLANG_RHI_ASSERT(api.vkGetPhysicalDeviceFeatures2);
    api.vkGetPhysicalDeviceProperties2(physicalDevice, &props);
    if (idProps.deviceLUIDValid)
    {
        SLANG_RHI_ASSERT(sizeof(AdapterLUID) >= VK_LUID_SIZE);
        memcpy(&luid, idProps.deviceLUID, VK_LUID_SIZE);
    }
    else
    {
        SLANG_RHI_ASSERT(sizeof(AdapterLUID) >= VK_UUID_SIZE);
        memcpy(&luid, idProps.deviceUUID, VK_UUID_SIZE);
    }

    return luid;
}

} // namespace rhi::vk

namespace rhi {

Result SLANG_MCALL getVKAdapters(std::vector<AdapterInfo>& outAdapters)
{
    for (int forceSoftware = 0; forceSoftware <= 1; forceSoftware++)
    {
        vk::VulkanModule module;
        if (module.init(forceSoftware != 0) != SLANG_OK)
            continue;
        vk::VulkanApi api;
        if (api.initGlobalProcs(module) != SLANG_OK)
            continue;

        VkInstanceCreateInfo instanceCreateInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        const char* instanceExtensions[] = {
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#if SLANG_APPLE_FAMILY
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
        };
        instanceCreateInfo.enabledExtensionCount = SLANG_COUNT_OF(instanceExtensions);
        instanceCreateInfo.ppEnabledExtensionNames = &instanceExtensions[0];
#if SLANG_APPLE_FAMILY
        instanceCreateInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
        VkInstance instance;
        SLANG_VK_RETURN_ON_FAIL(api.vkCreateInstance(&instanceCreateInfo, nullptr, &instance));

        // This will fail due to not loading any extensions.
        api.initInstanceProcs(instance);

        // Make sure required functions for enumerating physical devices were loaded.
        if (api.vkEnumeratePhysicalDevices || api.vkGetPhysicalDeviceProperties)
        {
            uint32_t numPhysicalDevices = 0;
            SLANG_VK_RETURN_ON_FAIL(api.vkEnumeratePhysicalDevices(instance, &numPhysicalDevices, nullptr));

            std::vector<VkPhysicalDevice> physicalDevices;
            physicalDevices.resize(numPhysicalDevices);
            SLANG_VK_RETURN_ON_FAIL(
                api.vkEnumeratePhysicalDevices(instance, &numPhysicalDevices, physicalDevices.data())
            );

            for (const auto& physicalDevice : physicalDevices)
            {
                VkPhysicalDeviceProperties props;
                api.vkGetPhysicalDeviceProperties(physicalDevice, &props);
                AdapterInfo info = {};
                memcpy(info.name, props.deviceName, min(strlen(props.deviceName), sizeof(AdapterInfo::name) - 1));
                info.vendorID = props.vendorID;
                info.deviceID = props.deviceID;
                info.luid = vk::getAdapterLUID(api, physicalDevice);
                outAdapters.push_back(info);
            }
        }

        api.vkDestroyInstance(instance, nullptr);
        module.destroy();
    }

    return SLANG_OK;
}

Result SLANG_MCALL createVKDevice(const DeviceDesc* desc, IDevice** outRenderer)
{
    RefPtr<vk::DeviceImpl> result = new vk::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outRenderer, result);
    return SLANG_OK;
}

} // namespace rhi

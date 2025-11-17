#include "vk-utils.h"

#include "core/common.h"

namespace rhi::vk {

void reportVulkanError(VkResult res)
{
    SLANG_RHI_ASSERT_FAILURE("Vulkan returned a failure");
}

VkFormat getVkFormat(Format format)
{
    switch (format)
    {
    case Format::R8Uint:
        return VK_FORMAT_R8_UINT;
    case Format::R8Sint:
        return VK_FORMAT_R8_SINT;
    case Format::R8Unorm:
        return VK_FORMAT_R8_UNORM;
    case Format::R8Snorm:
        return VK_FORMAT_R8_SNORM;

    case Format::RG8Uint:
        return VK_FORMAT_R8G8_UINT;
    case Format::RG8Sint:
        return VK_FORMAT_R8G8_SINT;
    case Format::RG8Unorm:
        return VK_FORMAT_R8G8_UNORM;
    case Format::RG8Snorm:
        return VK_FORMAT_R8G8_SNORM;

    case Format::RGBA8Uint:
        return VK_FORMAT_R8G8B8A8_UINT;
    case Format::RGBA8Sint:
        return VK_FORMAT_R8G8B8A8_SINT;
    case Format::RGBA8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8UnormSrgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::RGBA8Snorm:
        return VK_FORMAT_R8G8B8A8_SNORM;

    case Format::BGRA8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8UnormSrgb:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::BGRX8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRX8UnormSrgb:
        return VK_FORMAT_B8G8R8A8_SRGB;

    case Format::R16Uint:
        return VK_FORMAT_R16_UINT;
    case Format::R16Sint:
        return VK_FORMAT_R16_SINT;
    case Format::R16Unorm:
        return VK_FORMAT_R16_UNORM;
    case Format::R16Snorm:
        return VK_FORMAT_R16_SNORM;
    case Format::R16Float:
        return VK_FORMAT_R16_SFLOAT;

    case Format::RG16Uint:
        return VK_FORMAT_R16G16_UINT;
    case Format::RG16Sint:
        return VK_FORMAT_R16G16_SINT;
    case Format::RG16Unorm:
        return VK_FORMAT_R16G16_UNORM;
    case Format::RG16Snorm:
        return VK_FORMAT_R16G16_SNORM;
    case Format::RG16Float:
        return VK_FORMAT_R16G16_SFLOAT;

    case Format::RGBA16Uint:
        return VK_FORMAT_R16G16B16A16_UINT;
    case Format::RGBA16Sint:
        return VK_FORMAT_R16G16B16A16_SINT;
    case Format::RGBA16Unorm:
        return VK_FORMAT_R16G16B16A16_UNORM;
    case Format::RGBA16Snorm:
        return VK_FORMAT_R16G16B16A16_SNORM;
    case Format::RGBA16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;

    case Format::R32Uint:
        return VK_FORMAT_R32_UINT;
    case Format::R32Sint:
        return VK_FORMAT_R32_SINT;
    case Format::R32Float:
        return VK_FORMAT_R32_SFLOAT;

    case Format::RG32Uint:
        return VK_FORMAT_R32G32_UINT;
    case Format::RG32Sint:
        return VK_FORMAT_R32G32_SINT;
    case Format::RG32Float:
        return VK_FORMAT_R32G32_SFLOAT;

    case Format::RGB32Uint:
        return VK_FORMAT_R32G32B32_UINT;
    case Format::RGB32Sint:
        return VK_FORMAT_R32G32B32_SINT;
    case Format::RGB32Float:
        return VK_FORMAT_R32G32B32_SFLOAT;

    case Format::RGBA32Uint:
        return VK_FORMAT_R32G32B32A32_UINT;
    case Format::RGBA32Sint:
        return VK_FORMAT_R32G32B32A32_SINT;
    case Format::RGBA32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;

    case Format::R64Uint:
        return VK_FORMAT_R64_UINT;
    case Format::R64Sint:
        return VK_FORMAT_R64_SINT;

    case Format::BGRA4Unorm:
        return VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT;
    case Format::B5G6R5Unorm:
        return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case Format::BGR5A1Unorm:
        return VK_FORMAT_A1R5G5B5_UNORM_PACK16;

    case Format::RGB9E5Ufloat:
        return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    case Format::RGB10A2Uint:
        return VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case Format::RGB10A2Unorm:
        return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case Format::R11G11B10Float:
        return VK_FORMAT_B10G11R11_UFLOAT_PACK32;

    case Format::D32Float:
        return VK_FORMAT_D32_SFLOAT;
    case Format::D16Unorm:
        return VK_FORMAT_D16_UNORM;
    case Format::D32FloatS8Uint:
        return VK_FORMAT_D32_SFLOAT_S8_UINT;

    case Format::BC1Unorm:
        return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Format::BC1UnormSrgb:
        return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case Format::BC2Unorm:
        return VK_FORMAT_BC2_UNORM_BLOCK;
    case Format::BC2UnormSrgb:
        return VK_FORMAT_BC2_SRGB_BLOCK;
    case Format::BC3Unorm:
        return VK_FORMAT_BC3_UNORM_BLOCK;
    case Format::BC3UnormSrgb:
        return VK_FORMAT_BC3_SRGB_BLOCK;
    case Format::BC4Unorm:
        return VK_FORMAT_BC4_UNORM_BLOCK;
    case Format::BC4Snorm:
        return VK_FORMAT_BC4_SNORM_BLOCK;
    case Format::BC5Unorm:
        return VK_FORMAT_BC5_UNORM_BLOCK;
    case Format::BC5Snorm:
        return VK_FORMAT_BC5_SNORM_BLOCK;
    case Format::BC6HUfloat:
        return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    case Format::BC6HSfloat:
        return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    case Format::BC7Unorm:
        return VK_FORMAT_BC7_UNORM_BLOCK;
    case Format::BC7UnormSrgb:
        return VK_FORMAT_BC7_SRGB_BLOCK;

    default:
        return VK_FORMAT_UNDEFINED;
    }
}

VkAttachmentLoadOp translateLoadOp(LoadOp loadOp)
{
    switch (loadOp)
    {
    case LoadOp::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::DontCare:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid LoadOp value");
    return VkAttachmentLoadOp(0);
}

VkAttachmentStoreOp translateStoreOp(StoreOp storeOp)
{
    switch (storeOp)
    {
    case StoreOp::Store:
        return VK_ATTACHMENT_STORE_OP_STORE;
    case StoreOp::DontCare:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid StoreOp value");
    return VkAttachmentStoreOp(0);
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

VkPipelineCreateFlags2 translateRayTracingPipelineFlags2(RayTracingPipelineFlags flags)
{
    // The lower bits of the extended flags are the same as the non-extended version, so we can share logic with that.
    VkPipelineCreateFlags2 vkFlags = translateRayTracingPipelineFlags(flags);

    // Now, handle any flags specific to the extended version.
    if (is_set(flags, RayTracingPipelineFlags::EnableSpheres) ||
        is_set(flags, RayTracingPipelineFlags::EnableLinearSweptSpheres))
        vkFlags |= VK_PIPELINE_CREATE_2_RAY_TRACING_ALLOW_SPHERES_AND_LINEAR_SWEPT_SPHERES_BIT_NV;

    return vkFlags;
}

VkImageLayout translateImageLayout(ResourceState state)
{
    switch (state)
    {
    case ResourceState::Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case ResourceState::General:
        return VK_IMAGE_LAYOUT_GENERAL;
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
        result |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
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

VkImageUsageFlagBits _calcImageUsageFlags(TextureUsage usage)
{
    int flags = 0;
    if (is_set(usage, TextureUsage::ShaderResource))
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (is_set(usage, TextureUsage::UnorderedAccess))
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (is_set(usage, TextureUsage::RenderTarget))
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (is_set(usage, TextureUsage::DepthStencil))
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
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureAspect value");
    return VkImageAspectFlags(0);
}

AdapterLUID getAdapterLUID(const VkPhysicalDeviceIDProperties& props)
{
    AdapterLUID luid = {};
    if (props.deviceLUIDValid)
    {
        static_assert(sizeof(AdapterLUID) >= VK_LUID_SIZE);
        memcpy(&luid, props.deviceLUID, VK_LUID_SIZE);
    }
    else
    {
        static_assert(sizeof(AdapterLUID) >= VK_UUID_SIZE);
        memcpy(&luid, props.deviceUUID, VK_UUID_SIZE);
    }
    return luid;
}

VkShaderStageFlags translateShaderStage(SlangStage stage)
{
    switch (stage)
    {
    case SLANG_STAGE_ANY_HIT:
        return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    case SLANG_STAGE_CALLABLE:
        return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
    case SLANG_STAGE_CLOSEST_HIT:
        return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    case SLANG_STAGE_COMPUTE:
        return VK_SHADER_STAGE_COMPUTE_BIT;
    case SLANG_STAGE_DOMAIN:
        return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case SLANG_STAGE_FRAGMENT:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case SLANG_STAGE_GEOMETRY:
        return VK_SHADER_STAGE_GEOMETRY_BIT;
    case SLANG_STAGE_HULL:
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    case SLANG_STAGE_INTERSECTION:
        return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    case SLANG_STAGE_MISS:
        return VK_SHADER_STAGE_MISS_BIT_KHR;
    case SLANG_STAGE_RAY_GENERATION:
        return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    case SLANG_STAGE_VERTEX:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case SLANG_STAGE_MESH:
        return VK_SHADER_STAGE_MESH_BIT_EXT;
    case SLANG_STAGE_AMPLIFICATION:
        return VK_SHADER_STAGE_TASK_BIT_EXT;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported stage.");
        return VkShaderStageFlags(-1);
    }
}

VkImageLayout getImageLayoutFromState(ResourceState state)
{
    switch (state)
    {
    case ResourceState::ShaderResource:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ResourceState::UnorderedAccess:
    case ResourceState::General:
        return VK_IMAGE_LAYOUT_GENERAL;
    case ResourceState::Present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    case ResourceState::CopySource:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ResourceState::CopyDestination:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case ResourceState::RenderTarget:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthWrite:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthRead:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case ResourceState::ResolveSource:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ResourceState::ResolveDestination:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    default:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return VkImageLayout();
}

bool isDepthFormat(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

bool isStencilFormat(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

VkSampleCountFlagBits translateSampleCount(uint32_t sampleCount)
{
    switch (sampleCount)
    {
    case 1:
        return VK_SAMPLE_COUNT_1_BIT;
    case 2:
        return VK_SAMPLE_COUNT_2_BIT;
    case 4:
        return VK_SAMPLE_COUNT_4_BIT;
    case 8:
        return VK_SAMPLE_COUNT_8_BIT;
    case 16:
        return VK_SAMPLE_COUNT_16_BIT;
    case 32:
        return VK_SAMPLE_COUNT_32_BIT;
    case 64:
        return VK_SAMPLE_COUNT_64_BIT;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported sample count");
        return VK_SAMPLE_COUNT_1_BIT;
    }
}

VkCullModeFlags translateCullMode(CullMode cullMode)
{
    switch (cullMode)
    {
    case CullMode::None:
        return VK_CULL_MODE_NONE;
    case CullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back:
        return VK_CULL_MODE_BACK_BIT;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid CullMode value");
    return VkCullModeFlags(0);
}

VkFrontFace translateFrontFaceMode(FrontFaceMode frontFaceMode)
{
    switch (frontFaceMode)
    {
    case FrontFaceMode::CounterClockwise:
        return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    case FrontFaceMode::Clockwise:
        return VK_FRONT_FACE_CLOCKWISE;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid FrontFaceMode value");
    return VkFrontFace(0);
}

VkPolygonMode translateFillMode(FillMode fillMode)
{
    switch (fillMode)
    {
    case FillMode::Solid:
        return VK_POLYGON_MODE_FILL;
    case FillMode::Wireframe:
        return VK_POLYGON_MODE_LINE;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid FillMode value");
    return VkPolygonMode(0);
}

VkBlendFactor translateBlendFactor(BlendFactor blendFactor)
{
    switch (blendFactor)
    {
    case BlendFactor::Zero:
        return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:
        return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcColor:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::InvSrcColor:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DestAlpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::InvDestAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::DestColor:
        return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::InvDestColor:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::SrcAlphaSaturate:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case BlendFactor::BlendColor:
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case BlendFactor::InvBlendColor:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case BlendFactor::SecondarySrcColor:
        return VK_BLEND_FACTOR_SRC1_COLOR;
    case BlendFactor::InvSecondarySrcColor:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    case BlendFactor::SecondarySrcAlpha:
        return VK_BLEND_FACTOR_SRC1_ALPHA;
    case BlendFactor::InvSecondarySrcAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid BlendFactor value");
    return VkBlendFactor(0);
}

VkBlendOp translateBlendOp(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add:
        return VK_BLEND_OP_ADD;
    case BlendOp::Subtract:
        return VK_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min:
        return VK_BLEND_OP_MIN;
    case BlendOp::Max:
        return VK_BLEND_OP_MAX;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid BlendOp value");
    return VkBlendOp(0);
}

VkPrimitiveTopology translatePrimitiveListTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::PointList:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveTopology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LineStrip:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::TriangleList:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::PatchList:
        return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid PrimitiveTopology value");
    return VkPrimitiveTopology(0);
}

VkStencilOp translateStencilOp(StencilOp op)
{
    switch (op)
    {
    case StencilOp::DecrementSaturate:
        return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case StencilOp::DecrementWrap:
        return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    case StencilOp::IncrementSaturate:
        return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case StencilOp::IncrementWrap:
        return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case StencilOp::Invert:
        return VK_STENCIL_OP_INVERT;
    case StencilOp::Keep:
        return VK_STENCIL_OP_KEEP;
    case StencilOp::Replace:
        return VK_STENCIL_OP_REPLACE;
    case StencilOp::Zero:
        return VK_STENCIL_OP_ZERO;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid StencilOp value");
    return VkStencilOp(0);
}

VkFilter translateFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return VK_FILTER_NEAREST;
    case TextureFilteringMode::Linear:
        return VK_FILTER_LINEAR;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureFilteringMode value");
    return VkFilter(0);
}

VkSamplerMipmapMode translateMipFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case TextureFilteringMode::Linear:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureFilteringMode value");
    return VkSamplerMipmapMode(0);
}

VkSamplerAddressMode translateAddressingMode(TextureAddressingMode mode)
{
    switch (mode)
    {
    case TextureAddressingMode::Wrap:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case TextureAddressingMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case TextureAddressingMode::ClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case TextureAddressingMode::MirrorRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case TextureAddressingMode::MirrorOnce:
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureAddressingMode value");
    return VkSamplerAddressMode(0);
}

VkCompareOp translateComparisonFunc(ComparisonFunc func)
{
    switch (func)
    {
    case ComparisonFunc::Never:
        return VK_COMPARE_OP_NEVER;
    case ComparisonFunc::Less:
        return VK_COMPARE_OP_LESS;
    case ComparisonFunc::Equal:
        return VK_COMPARE_OP_EQUAL;
    case ComparisonFunc::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case ComparisonFunc::Greater:
        return VK_COMPARE_OP_GREATER;
    case ComparisonFunc::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case ComparisonFunc::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case ComparisonFunc::Always:
        return VK_COMPARE_OP_ALWAYS;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid ComparisonFunc value");
    return VkCompareOp(0);
}

VkStencilOpState translateStencilState(DepthStencilOpDesc desc)
{
    VkStencilOpState rs;
    rs.compareMask = 0xFF;
    rs.compareOp = translateComparisonFunc(desc.stencilFunc);
    rs.depthFailOp = translateStencilOp(desc.stencilDepthFailOp);
    rs.failOp = translateStencilOp(desc.stencilFailOp);
    rs.passOp = translateStencilOp(desc.stencilPassOp);
    rs.reference = 0;
    rs.writeMask = 0xFF;
    return rs;
}

VkSamplerReductionMode translateReductionOp(TextureReductionOp op)
{
    switch (op)
    {
    case TextureReductionOp::Average:
        return VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
    case TextureReductionOp::Comparison:
        return VkSamplerReductionMode(0); // set through compareEnable
    case TextureReductionOp::Minimum:
        return VK_SAMPLER_REDUCTION_MODE_MIN;
    case TextureReductionOp::Maximum:
        return VK_SAMPLER_REDUCTION_MODE_MAX;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureReductionOp value");
    return VkSamplerReductionMode(0);
}

VkComponentTypeKHR translateCooperativeVectorComponentType(CooperativeVectorComponentType type)
{
    switch (type)
    {
    case CooperativeVectorComponentType::Float16:
        return VK_COMPONENT_TYPE_FLOAT16_KHR;
    case CooperativeVectorComponentType::Float32:
        return VK_COMPONENT_TYPE_FLOAT32_KHR;
    case CooperativeVectorComponentType::Float64:
        return VK_COMPONENT_TYPE_FLOAT64_KHR;
    case CooperativeVectorComponentType::Sint8:
        return VK_COMPONENT_TYPE_SINT8_KHR;
    case CooperativeVectorComponentType::Sint16:
        return VK_COMPONENT_TYPE_SINT16_KHR;
    case CooperativeVectorComponentType::Sint32:
        return VK_COMPONENT_TYPE_SINT32_KHR;
    case CooperativeVectorComponentType::Sint64:
        return VK_COMPONENT_TYPE_SINT64_KHR;
    case CooperativeVectorComponentType::Uint8:
        return VK_COMPONENT_TYPE_UINT8_KHR;
    case CooperativeVectorComponentType::Uint16:
        return VK_COMPONENT_TYPE_UINT16_KHR;
    case CooperativeVectorComponentType::Uint32:
        return VK_COMPONENT_TYPE_UINT32_KHR;
    case CooperativeVectorComponentType::Uint64:
        return VK_COMPONENT_TYPE_UINT64_KHR;
    case CooperativeVectorComponentType::Sint8Packed:
        return VK_COMPONENT_TYPE_SINT8_PACKED_NV;
    case CooperativeVectorComponentType::Uint8Packed:
        return VK_COMPONENT_TYPE_UINT8_PACKED_NV;
    case CooperativeVectorComponentType::FloatE4M3:
        return VK_COMPONENT_TYPE_FLOAT_E4M3_NV;
    case CooperativeVectorComponentType::FloatE5M2:
        return VK_COMPONENT_TYPE_FLOAT_E5M2_NV;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid CooperativeVectorComponentType value");
    return VkComponentTypeKHR(0);
}

CooperativeVectorComponentType translateCooperativeVectorComponentType(VkComponentTypeKHR type)
{
    switch (type)
    {
    case VK_COMPONENT_TYPE_FLOAT16_KHR:
        return CooperativeVectorComponentType::Float16;
    case VK_COMPONENT_TYPE_FLOAT32_KHR:
        return CooperativeVectorComponentType::Float32;
    case VK_COMPONENT_TYPE_FLOAT64_KHR:
        return CooperativeVectorComponentType::Float64;
    case VK_COMPONENT_TYPE_SINT8_KHR:
        return CooperativeVectorComponentType::Sint8;
    case VK_COMPONENT_TYPE_SINT16_KHR:
        return CooperativeVectorComponentType::Sint16;
    case VK_COMPONENT_TYPE_SINT32_KHR:
        return CooperativeVectorComponentType::Sint32;
    case VK_COMPONENT_TYPE_SINT64_KHR:
        return CooperativeVectorComponentType::Sint64;
    case VK_COMPONENT_TYPE_UINT8_KHR:
        return CooperativeVectorComponentType::Uint8;
    case VK_COMPONENT_TYPE_UINT16_KHR:
        return CooperativeVectorComponentType::Uint16;
    case VK_COMPONENT_TYPE_UINT32_KHR:
        return CooperativeVectorComponentType::Uint32;
    case VK_COMPONENT_TYPE_UINT64_KHR:
        return CooperativeVectorComponentType::Uint64;
    case VK_COMPONENT_TYPE_SINT8_PACKED_NV:
        return CooperativeVectorComponentType::Sint8Packed;
    case VK_COMPONENT_TYPE_UINT8_PACKED_NV:
        return CooperativeVectorComponentType::Uint8Packed;
    case VK_COMPONENT_TYPE_FLOAT_E4M3_NV:
        return CooperativeVectorComponentType::FloatE4M3;
    case VK_COMPONENT_TYPE_FLOAT_E5M2_NV:
        return CooperativeVectorComponentType::FloatE5M2;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported VkComponentTypeKHR value");
        return CooperativeVectorComponentType(0);
    }
}

VkCooperativeVectorMatrixLayoutNV translateCooperativeVectorMatrixLayout(CooperativeVectorMatrixLayout layout)
{
    switch (layout)
    {
    case CooperativeVectorMatrixLayout::RowMajor:
        return VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR_NV;
    case CooperativeVectorMatrixLayout::ColumnMajor:
        return VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_COLUMN_MAJOR_NV;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
        return VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_INFERENCING_OPTIMAL_NV;
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        return VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_TRAINING_OPTIMAL_NV;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid CooperativeVectorMatrixLayout value");
    return VkCooperativeVectorMatrixLayoutNV(0);
}

CooperativeVectorMatrixLayout translateCooperativeVectorMatrixLayout(VkCooperativeVectorMatrixLayoutNV layout)
{
    switch (layout)
    {
    case VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR_NV:
        return CooperativeVectorMatrixLayout::RowMajor;
    case VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_COLUMN_MAJOR_NV:
        return CooperativeVectorMatrixLayout::ColumnMajor;
    case VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_INFERENCING_OPTIMAL_NV:
        return CooperativeVectorMatrixLayout::InferencingOptimal;
    case VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_TRAINING_OPTIMAL_NV:
        return CooperativeVectorMatrixLayout::TrainingOptimal;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported VkCooperativeVectorMatrixLayoutNV value");
        return CooperativeVectorMatrixLayout(0);
    }
}

} // namespace rhi::vk

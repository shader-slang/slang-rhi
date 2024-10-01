#include "vk-util.h"

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>

namespace rhi::vk {

VkFormat VulkanUtil::getVkFormat(Format format)
{
    switch (format)
    {
    case Format::R32G32B32A32_TYPELESS:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::R32G32B32_TYPELESS:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::R32G32_TYPELESS:
        return VK_FORMAT_R32G32_SFLOAT;
    case Format::R32_TYPELESS:
        return VK_FORMAT_R32_SFLOAT;

    case Format::R16G16B16A16_TYPELESS:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R16G16_TYPELESS:
        return VK_FORMAT_R16G16_SFLOAT;
    case Format::R16_TYPELESS:
        return VK_FORMAT_R16_SFLOAT;

    case Format::R8G8B8A8_TYPELESS:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::R8G8_TYPELESS:
        return VK_FORMAT_R8G8_UNORM;
    case Format::R8_TYPELESS:
        return VK_FORMAT_R8_UNORM;
    case Format::B8G8R8A8_TYPELESS:
        return VK_FORMAT_B8G8R8A8_UNORM;

    case Format::R64_UINT:
        return VK_FORMAT_R64_UINT;

    case Format::R32G32B32A32_FLOAT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::R32G32B32_FLOAT:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::R32G32_FLOAT:
        return VK_FORMAT_R32G32_SFLOAT;
    case Format::R32_FLOAT:
        return VK_FORMAT_R32_SFLOAT;

    case Format::R16G16B16A16_FLOAT:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R16G16_FLOAT:
        return VK_FORMAT_R16G16_SFLOAT;
    case Format::R16_FLOAT:
        return VK_FORMAT_R16_SFLOAT;

    case Format::R32G32B32A32_UINT:
        return VK_FORMAT_R32G32B32A32_UINT;
    case Format::R32G32B32_UINT:
        return VK_FORMAT_R32G32B32_UINT;
    case Format::R32G32_UINT:
        return VK_FORMAT_R32G32_UINT;
    case Format::R32_UINT:
        return VK_FORMAT_R32_UINT;

    case Format::R16G16B16A16_UINT:
        return VK_FORMAT_R16G16B16A16_UINT;
    case Format::R16G16_UINT:
        return VK_FORMAT_R16G16_UINT;
    case Format::R16_UINT:
        return VK_FORMAT_R16_UINT;

    case Format::R8G8B8A8_UINT:
        return VK_FORMAT_R8G8B8A8_UINT;
    case Format::R8G8_UINT:
        return VK_FORMAT_R8G8_UINT;
    case Format::R8_UINT:
        return VK_FORMAT_R8_UINT;

    case Format::R64_SINT:
        return VK_FORMAT_R64_SINT;

    case Format::R32G32B32A32_SINT:
        return VK_FORMAT_R32G32B32A32_SINT;
    case Format::R32G32B32_SINT:
        return VK_FORMAT_R32G32B32_SINT;
    case Format::R32G32_SINT:
        return VK_FORMAT_R32G32_SINT;
    case Format::R32_SINT:
        return VK_FORMAT_R32_SINT;

    case Format::R16G16B16A16_SINT:
        return VK_FORMAT_R16G16B16A16_SINT;
    case Format::R16G16_SINT:
        return VK_FORMAT_R16G16_SINT;
    case Format::R16_SINT:
        return VK_FORMAT_R16_SINT;

    case Format::R8G8B8A8_SINT:
        return VK_FORMAT_R8G8B8A8_SINT;
    case Format::R8G8_SINT:
        return VK_FORMAT_R8G8_SINT;
    case Format::R8_SINT:
        return VK_FORMAT_R8_SINT;

    case Format::R16G16B16A16_UNORM:
        return VK_FORMAT_R16G16B16A16_UNORM;
    case Format::R16G16_UNORM:
        return VK_FORMAT_R16G16_UNORM;
    case Format::R16_UNORM:
        return VK_FORMAT_R16_UNORM;

    case Format::R8G8B8A8_UNORM:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::R8G8B8A8_UNORM_SRGB:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::R8G8_UNORM:
        return VK_FORMAT_R8G8_UNORM;
    case Format::R8_UNORM:
        return VK_FORMAT_R8_UNORM;
    case Format::B8G8R8A8_UNORM:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::B8G8R8A8_UNORM_SRGB:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::B8G8R8X8_UNORM:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::B8G8R8X8_UNORM_SRGB:
        return VK_FORMAT_B8G8R8A8_SRGB;

    case Format::R16G16B16A16_SNORM:
        return VK_FORMAT_R16G16B16A16_SNORM;
    case Format::R16G16_SNORM:
        return VK_FORMAT_R16G16_SNORM;
    case Format::R16_SNORM:
        return VK_FORMAT_R16_SNORM;

    case Format::R8G8B8A8_SNORM:
        return VK_FORMAT_R8G8B8A8_SNORM;
    case Format::R8G8_SNORM:
        return VK_FORMAT_R8G8_SNORM;
    case Format::R8_SNORM:
        return VK_FORMAT_R8_SNORM;

    case Format::D32_FLOAT:
        return VK_FORMAT_D32_SFLOAT;
    case Format::D16_UNORM:
        return VK_FORMAT_D16_UNORM;
    case Format::D32_FLOAT_S8_UINT:
        return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case Format::R32_FLOAT_X32_TYPELESS:
        return VK_FORMAT_R32_SFLOAT;

    case Format::B4G4R4A4_UNORM:
        return VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT;
    case Format::B5G6R5_UNORM:
        return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case Format::B5G5R5A1_UNORM:
        return VK_FORMAT_A1R5G5B5_UNORM_PACK16;

    case Format::R9G9B9E5_SHAREDEXP:
        return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    case Format::R10G10B10A2_TYPELESS:
        return VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case Format::R10G10B10A2_UINT:
        return VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case Format::R10G10B10A2_UNORM:
        return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case Format::R11G11B10_FLOAT:
        return VK_FORMAT_B10G11R11_UFLOAT_PACK32;

    case Format::BC1_UNORM:
        return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Format::BC1_UNORM_SRGB:
        return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case Format::BC2_UNORM:
        return VK_FORMAT_BC2_UNORM_BLOCK;
    case Format::BC2_UNORM_SRGB:
        return VK_FORMAT_BC2_SRGB_BLOCK;
    case Format::BC3_UNORM:
        return VK_FORMAT_BC3_UNORM_BLOCK;
    case Format::BC3_UNORM_SRGB:
        return VK_FORMAT_BC3_SRGB_BLOCK;
    case Format::BC4_UNORM:
        return VK_FORMAT_BC4_UNORM_BLOCK;
    case Format::BC4_SNORM:
        return VK_FORMAT_BC4_SNORM_BLOCK;
    case Format::BC5_UNORM:
        return VK_FORMAT_BC5_UNORM_BLOCK;
    case Format::BC5_SNORM:
        return VK_FORMAT_BC5_SNORM_BLOCK;
    case Format::BC6H_UF16:
        return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    case Format::BC6H_SF16:
        return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    case Format::BC7_UNORM:
        return VK_FORMAT_BC7_UNORM_BLOCK;
    case Format::BC7_UNORM_SRGB:
        return VK_FORMAT_BC7_SRGB_BLOCK;

    default:
        return VK_FORMAT_UNDEFINED;
    }
}

VkImageAspectFlags VulkanUtil::getAspectMask(TextureAspect aspect, VkFormat format)
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
        SLANG_RHI_UNREACHABLE("getAspectMask");
        return 0;
    }
}

Result VulkanUtil::toResult(VkResult res)
{
    return (res == VK_SUCCESS) ? SLANG_OK : SLANG_FAIL;
}

VkShaderStageFlags VulkanUtil::getShaderStage(SlangStage stage)
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

VkImageLayout VulkanUtil::getImageLayoutFromState(ResourceState state)
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

VkSampleCountFlagBits VulkanUtil::translateSampleCount(uint32_t sampleCount)
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

VkCullModeFlags VulkanUtil::translateCullMode(CullMode cullMode)
{
    switch (cullMode)
    {
    case CullMode::None:
        return VK_CULL_MODE_NONE;
    case CullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back:
        return VK_CULL_MODE_BACK_BIT;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported cull mode");
        return VK_CULL_MODE_NONE;
    }
}

VkFrontFace VulkanUtil::translateFrontFaceMode(FrontFaceMode frontFaceMode)
{
    switch (frontFaceMode)
    {
    case FrontFaceMode::CounterClockwise:
        return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    case FrontFaceMode::Clockwise:
        return VK_FRONT_FACE_CLOCKWISE;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported front face mode");
        return VK_FRONT_FACE_CLOCKWISE;
    }
}

VkPolygonMode VulkanUtil::translateFillMode(FillMode fillMode)
{
    switch (fillMode)
    {
    case FillMode::Solid:
        return VK_POLYGON_MODE_FILL;
    case FillMode::Wireframe:
        return VK_POLYGON_MODE_LINE;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported fill mode");
        return VK_POLYGON_MODE_FILL;
    }
}

VkBlendFactor VulkanUtil::translateBlendFactor(BlendFactor blendFactor)
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
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
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

    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported blend factor");
        return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp VulkanUtil::translateBlendOp(BlendOp op)
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
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported blend op");
        return VK_BLEND_OP_ADD;
    }
}

VkPrimitiveTopology VulkanUtil::translatePrimitiveListTopology(PrimitiveTopology topology)
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
    default:
        SLANG_RHI_ASSERT_FAILURE("Unknown topology type.");
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkStencilOp VulkanUtil::translateStencilOp(StencilOp op)
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
    default:
        return VK_STENCIL_OP_KEEP;
    }
}

VkFilter VulkanUtil::translateFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    default:
        return VkFilter(0);

#define CASE(SRC, DST)                                                                                                 \
    case TextureFilteringMode::SRC:                                                                                    \
        return VK_FILTER_##DST

        CASE(Point, NEAREST);
        CASE(Linear, LINEAR);

#undef CASE
    }
}

VkSamplerMipmapMode VulkanUtil::translateMipFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    default:
        return VkSamplerMipmapMode(0);

#define CASE(SRC, DST)                                                                                                 \
    case TextureFilteringMode::SRC:                                                                                    \
        return VK_SAMPLER_MIPMAP_MODE_##DST

        CASE(Point, NEAREST);
        CASE(Linear, LINEAR);

#undef CASE
    }
}

VkSamplerAddressMode VulkanUtil::translateAddressingMode(TextureAddressingMode mode)
{
    switch (mode)
    {
    default:
        return VkSamplerAddressMode(0);

#define CASE(SRC, DST)                                                                                                 \
    case TextureAddressingMode::SRC:                                                                                   \
        return VK_SAMPLER_ADDRESS_MODE_##DST

        CASE(Wrap, REPEAT);
        CASE(ClampToEdge, CLAMP_TO_EDGE);
        CASE(ClampToBorder, CLAMP_TO_BORDER);
        CASE(MirrorRepeat, MIRRORED_REPEAT);
        CASE(MirrorOnce, MIRROR_CLAMP_TO_EDGE);

#undef CASE
    }
}

VkCompareOp VulkanUtil::translateComparisonFunc(ComparisonFunc func)
{
    switch (func)
    {
    default:
        // TODO: need to report failures
        return VK_COMPARE_OP_ALWAYS;

#define CASE(FROM, TO)                                                                                                 \
    case ComparisonFunc::FROM:                                                                                         \
        return VK_COMPARE_OP_##TO

        CASE(Never, NEVER);
        CASE(Less, LESS);
        CASE(Equal, EQUAL);
        CASE(LessEqual, LESS_OR_EQUAL);
        CASE(Greater, GREATER);
        CASE(NotEqual, NOT_EQUAL);
        CASE(GreaterEqual, GREATER_OR_EQUAL);
        CASE(Always, ALWAYS);
#undef CASE
    }
}

VkStencilOpState VulkanUtil::translateStencilState(DepthStencilOpDesc desc)
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

VkSamplerReductionMode VulkanUtil::translateReductionOp(TextureReductionOp op)
{
    switch (op)
    {
    case TextureReductionOp::Minimum:
        return VK_SAMPLER_REDUCTION_MODE_MIN;
    case TextureReductionOp::Maximum:
        return VK_SAMPLER_REDUCTION_MODE_MAX;
    default:
        return VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
    }
}

Result VulkanUtil::handleFail(VkResult res)
{
    if (res != VK_SUCCESS)
    {
        SLANG_RHI_ASSERT_FAILURE("Vulkan returned a failure");
    }
    return toResult(res);
}

void VulkanUtil::checkFail(VkResult res)
{
    SLANG_RHI_ASSERT(res != VK_SUCCESS);
    SLANG_RHI_ASSERT_FAILURE("Vulkan check failed");
}

VkPrimitiveTopology VulkanUtil::getVkPrimitiveTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LineStrip:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::TriangleList:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::PointList:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    default:
        break;
    }
    SLANG_RHI_ASSERT_FAILURE("Unknown topology");
    return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
}

VkImageLayout VulkanUtil::mapResourceStateToLayout(ResourceState state)
{
    switch (state)
    {
    case ResourceState::Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case ResourceState::ShaderResource:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ResourceState::UnorderedAccess:
        return VK_IMAGE_LAYOUT_GENERAL;
    case ResourceState::RenderTarget:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthRead:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case ResourceState::DepthWrite:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ResourceState::Present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    case ResourceState::CopySource:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ResourceState::CopyDestination:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case ResourceState::ResolveSource:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ResourceState::ResolveDestination:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    default:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

Result AccelerationStructureBuildGeometryInfoBuilder::build(
    const AccelerationStructureBuildDesc& buildDesc,
    IDebugCallback* debugCallback
)
{
    if (buildDesc.inputCount < 1)
    {
        return SLANG_E_INVALID_ARG;
    }

    AccelerationStructureBuildInputType type = (AccelerationStructureBuildInputType&)buildDesc.inputs[0];
    for (GfxIndex i = 0; i < buildDesc.inputCount; ++i)
    {
        if ((AccelerationStructureBuildInputType&)buildDesc.inputs[i] != type)
        {
            return SLANG_E_INVALID_ARG;
        }
    }

    switch (buildDesc.mode)
    {
    case AccelerationStructureBuildMode::Build:
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        break;
    case AccelerationStructureBuildMode::Update:
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }
    buildInfo.flags = translateBuildFlags(buildDesc.flags);
    geometries.resize(buildDesc.inputCount);
    primitiveCounts.resize(buildDesc.inputCount);
    buildInfo.pGeometries = geometries.data();
    buildInfo.geometryCount = geometries.size();

    switch (type)
    {
    case AccelerationStructureBuildInputType::Instances:
    {
        if (buildDesc.inputCount > 1)
        {
            return SLANG_E_INVALID_ARG;
        }
        const AccelerationStructureBuildInputInstances& instances =
            (const AccelerationStructureBuildInputInstances&)buildDesc.inputs[0];

        VkAccelerationStructureGeometryKHR& geometry = geometries[0];
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.arrayOfPointers = 0;
        geometry.geometry.instances.data.deviceAddress = instances.instanceBuffer.getDeviceAddress();

        primitiveCounts[0] = instances.instanceCount;

        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        break;
    }
    case AccelerationStructureBuildInputType::Triangles:
    {
        for (GfxIndex i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputTriangles& triangles =
                (const AccelerationStructureBuildInputTriangles&)buildDesc.inputs[i];
            if (triangles.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            VkAccelerationStructureGeometryKHR& geometry = geometries[i];
            geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.flags = translateGeometryFlags(triangles.flags);
            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = VulkanUtil::getVkFormat(triangles.vertexFormat);
            geometry.geometry.triangles.vertexData.deviceAddress = triangles.vertexBuffers[0].getDeviceAddress();
            geometry.geometry.triangles.vertexStride = triangles.vertexStride;
            geometry.geometry.triangles.maxVertex = triangles.vertexCount - 1;
            geometry.geometry.triangles.indexType =
                triangles.indexFormat == IndexFormat::UInt32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
            geometry.geometry.triangles.indexData.deviceAddress = triangles.indexBuffer.getDeviceAddress();
            geometry.geometry.triangles.transformData.deviceAddress =
                triangles.preTransformBuffer ? triangles.preTransformBuffer.getDeviceAddress() : 0;

            primitiveCounts[i] = std::max(triangles.vertexCount, triangles.indexCount) / 3;
        }

        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        break;
    }
    case AccelerationStructureBuildInputType::ProceduralPrimitives:
    {
        for (GfxIndex i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputProceduralPrimitives& proceduralPrimitives =
                (const AccelerationStructureBuildInputProceduralPrimitives&)buildDesc.inputs[i];
            if (proceduralPrimitives.aabbBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            VkAccelerationStructureGeometryKHR& geometry = geometries[i];
            geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
            geometry.flags = translateGeometryFlags(proceduralPrimitives.flags);
            geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
            geometry.geometry.aabbs.data.deviceAddress = proceduralPrimitives.aabbBuffers[0].getDeviceAddress();
            geometry.geometry.aabbs.stride = proceduralPrimitives.aabbStride;

            primitiveCounts[i] = proceduralPrimitives.primitiveCount;
        }

        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        break;
    }
    default:
        return SLANG_E_INVALID_ARG;
    }

    buildInfo.dstAccelerationStructure = VK_NULL_HANDLE;


#if 0
    switch (buildInputs.kind)
    {
    case IAccelerationStructure::Kind::BottomLevel:
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        break;
    case IAccelerationStructure::Kind::TopLevel:
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        break;
    default:
        debugCallback->handleMessage(
            DebugMessageType::Error,
            DebugMessageSource::Layer,
            "invalid value of IAccelerationStructure::Kind encountered in buildInputs.kind"
        );
        return SLANG_E_INVALID_ARG;
    }
    if (buildInputs.flags & IAccelerationStructure::BuildFlags::Enum::PerformUpdate)
    {
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    }
    else
    {
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    }
    if (buildInputs.flags & IAccelerationStructure::BuildFlags::Enum::AllowCompaction)
    {
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    }
    if (buildInputs.flags & IAccelerationStructure::BuildFlags::Enum::AllowUpdate)
    {
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }
    if (buildInputs.flags & IAccelerationStructure::BuildFlags::Enum::MinimizeMemory)
    {
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR;
    }
    if (buildInputs.flags & IAccelerationStructure::BuildFlags::Enum::PreferFastBuild)
    {
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    }
    if (buildInputs.flags & IAccelerationStructure::BuildFlags::Enum::PreferFastTrace)
    {
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    }
    if (buildInputs.kind == IAccelerationStructure::Kind::BottomLevel)
    {
        m_geometryInfos.resize(buildInputs.descCount);
        primitiveCounts.resize(buildInputs.descCount);
        memset(m_geometryInfos.data(), 0, sizeof(VkAccelerationStructureGeometryKHR) * buildInputs.descCount);
        for (int i = 0; i < buildInputs.descCount; i++)
        {
            auto& geomDesc = buildInputs.geometryDescs[i];
            m_geometryInfos[i].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            if (geomDesc.flags & IAccelerationStructure::GeometryFlags::NoDuplicateAnyHitInvocation)
            {
                m_geometryInfos[i].flags |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
            }
            else if (geomDesc.flags & IAccelerationStructure::GeometryFlags::Opaque)
            {
                m_geometryInfos[i].flags |= VK_GEOMETRY_OPAQUE_BIT_KHR;
            }
            auto& vkGeomData = m_geometryInfos[i].geometry;
            switch (geomDesc.type)
            {
            case IAccelerationStructure::GeometryType::Triangles:
                m_geometryInfos[i].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                vkGeomData.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                vkGeomData.triangles.vertexFormat = VulkanUtil::getVkFormat(geomDesc.content.triangles.vertexFormat);
                vkGeomData.triangles.vertexData.deviceAddress = geomDesc.content.triangles.vertexData;
                vkGeomData.triangles.vertexStride = geomDesc.content.triangles.vertexStride;
                vkGeomData.triangles.maxVertex = geomDesc.content.triangles.vertexCount - 1;
                switch (geomDesc.content.triangles.indexFormat)
                {
                case Format::R32_UINT:
                    vkGeomData.triangles.indexType = VK_INDEX_TYPE_UINT32;
                    break;
                case Format::R16_UINT:
                    vkGeomData.triangles.indexType = VK_INDEX_TYPE_UINT16;
                    break;
                case Format::Unknown:
                    vkGeomData.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
                    break;
                default:
                    debugCallback->handleMessage(
                        DebugMessageType::Error,
                        DebugMessageSource::Layer,
                        "unsupported value of Format encountered in "
                        "GeometryDesc::content.triangles.indexFormat"
                    );
                    return SLANG_E_INVALID_ARG;
                }
                vkGeomData.triangles.indexData.deviceAddress = geomDesc.content.triangles.indexData;
                vkGeomData.triangles.transformData.deviceAddress = geomDesc.content.triangles.transform3x4;
                primitiveCounts[i] =
                    std::max(geomDesc.content.triangles.vertexCount, geomDesc.content.triangles.indexCount) / 3;
                break;
            case IAccelerationStructure::GeometryType::ProcedurePrimitives:
                m_geometryInfos[i].geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
                vkGeomData.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
                vkGeomData.aabbs.data.deviceAddress = geomDesc.content.proceduralAABBs.data;
                vkGeomData.aabbs.stride = geomDesc.content.proceduralAABBs.stride;
                primitiveCounts[i] = (uint32_t)buildInputs.geometryDescs[i].content.proceduralAABBs.count;
                break;
            default:
                debugCallback->handleMessage(
                    DebugMessageType::Error,
                    DebugMessageSource::Layer,
                    "invalid value of IAccelerationStructure::GeometryType encountered in "
                    "buildInputs.geometryDescs"
                );
                return SLANG_E_INVALID_ARG;
            }
        }
        buildInfo.geometryCount = buildInputs.descCount;
        buildInfo.pGeometries = m_geometryInfos.data();
    }
    else
    {
        m_vkInstanceInfo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        m_vkInstanceInfo.geometry.instances.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        m_vkInstanceInfo.geometry.instances.arrayOfPointers = 0;
        m_vkInstanceInfo.geometry.instances.data.deviceAddress = buildInputs.instanceDescs;
        buildInfo.pGeometries = &m_vkInstanceInfo;
        buildInfo.geometryCount = 1;
        primitiveCounts.resize(1);
        primitiveCounts[0] = buildInputs.descCount;
    }
#endif
    return SLANG_OK;
}

} // namespace rhi::vk

#include "d3d11-utils.h"
#include "d3d11-device.h"

#include "core/string.h"

namespace rhi::d3d11 {

bool isSupportedNVAPIOp(IUnknown* dev, uint32_t op)
{
#if SLANG_RHI_ENABLE_NVAPI
    {
        bool isSupported;
        NvAPI_Status status = NvAPI_D3D11_IsNvShaderExtnOpCodeSupported(dev, NvU32(op), &isSupported);
        return status == NVAPI_OK && isSupported;
    }
#else
    return false;
#endif
}

UINT _calcResourceBindFlags(BufferUsage usage)
{
    UINT flags = 0;
    if (is_set(usage, BufferUsage::VertexBuffer))
        flags |= D3D11_BIND_VERTEX_BUFFER;
    if (is_set(usage, BufferUsage::IndexBuffer))
        flags |= D3D11_BIND_INDEX_BUFFER;
    if (is_set(usage, BufferUsage::ConstantBuffer))
        flags |= D3D11_BIND_CONSTANT_BUFFER;
    if (is_set(usage, BufferUsage::ShaderResource))
        flags |= D3D11_BIND_SHADER_RESOURCE;
    if (is_set(usage, BufferUsage::UnorderedAccess))
        flags |= D3D11_BIND_UNORDERED_ACCESS;
    return flags;
}

UINT _calcResourceBindFlags(TextureUsage usage)
{
    UINT flags = 0;
    if (is_set(usage, TextureUsage::RenderTarget))
        flags |= D3D11_BIND_RENDER_TARGET;
    if (is_set(usage, TextureUsage::DepthStencil))
        flags |= D3D11_BIND_DEPTH_STENCIL;
    if (is_set(usage, TextureUsage::ShaderResource))
        flags |= D3D11_BIND_SHADER_RESOURCE;
    if (is_set(usage, TextureUsage::UnorderedAccess))
        flags |= D3D11_BIND_UNORDERED_ACCESS;
    return flags;
}

UINT _calcResourceAccessFlags(MemoryType memType)
{
    switch (memType)
    {
    case MemoryType::DeviceLocal:
        return 0;
    case MemoryType::ReadBack:
        return D3D11_CPU_ACCESS_READ;
    case MemoryType::Upload:
        return D3D11_CPU_ACCESS_WRITE;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid MemoryType value");
    return 0;
}

D3D11_FILTER_TYPE translateFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return D3D11_FILTER_TYPE_POINT;
    case TextureFilteringMode::Linear:
        return D3D11_FILTER_TYPE_LINEAR;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureFilteringMode value");
    return D3D11_FILTER_TYPE(0);
}

D3D11_FILTER_REDUCTION_TYPE translateFilterReduction(TextureReductionOp op)
{
    switch (op)
    {
    case TextureReductionOp::Average:
        return D3D11_FILTER_REDUCTION_TYPE_STANDARD;
    case TextureReductionOp::Comparison:
        return D3D11_FILTER_REDUCTION_TYPE_COMPARISON;
    case TextureReductionOp::Minimum:
        return D3D11_FILTER_REDUCTION_TYPE_MINIMUM;
    case TextureReductionOp::Maximum:
        return D3D11_FILTER_REDUCTION_TYPE_MAXIMUM;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureReductionOp value");
    return D3D11_FILTER_REDUCTION_TYPE(0);
}

D3D11_TEXTURE_ADDRESS_MODE translateAddressingMode(TextureAddressingMode mode)
{
    switch (mode)
    {
    case TextureAddressingMode::Wrap:
        return D3D11_TEXTURE_ADDRESS_WRAP;
    case TextureAddressingMode::ClampToEdge:
        return D3D11_TEXTURE_ADDRESS_CLAMP;
    case TextureAddressingMode::ClampToBorder:
        return D3D11_TEXTURE_ADDRESS_BORDER;
    case TextureAddressingMode::MirrorRepeat:
        return D3D11_TEXTURE_ADDRESS_MIRROR;
    case TextureAddressingMode::MirrorOnce:
        return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureAddressingMode value");
    return D3D11_TEXTURE_ADDRESS_MODE(0);
}

D3D11_COMPARISON_FUNC translateComparisonFunc(ComparisonFunc func)
{
    switch (func)
    {
    case ComparisonFunc::Never:
        return D3D11_COMPARISON_NEVER;
    case ComparisonFunc::Less:
        return D3D11_COMPARISON_LESS;
    case ComparisonFunc::Equal:
        return D3D11_COMPARISON_EQUAL;
    case ComparisonFunc::LessEqual:
        return D3D11_COMPARISON_LESS_EQUAL;
    case ComparisonFunc::Greater:
        return D3D11_COMPARISON_GREATER;
    case ComparisonFunc::NotEqual:
        return D3D11_COMPARISON_NOT_EQUAL;
    case ComparisonFunc::GreaterEqual:
        return D3D11_COMPARISON_GREATER_EQUAL;
    case ComparisonFunc::Always:
        return D3D11_COMPARISON_ALWAYS;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid ComparisonFunc value");
    return D3D11_COMPARISON_FUNC(0);
}

D3D11_STENCIL_OP translateStencilOp(StencilOp op)
{
    switch (op)
    {
    case StencilOp::Keep:
        return D3D11_STENCIL_OP_KEEP;
    case StencilOp::Zero:
        return D3D11_STENCIL_OP_ZERO;
    case StencilOp::Replace:
        return D3D11_STENCIL_OP_REPLACE;
    case StencilOp::IncrementSaturate:
        return D3D11_STENCIL_OP_INCR_SAT;
    case StencilOp::DecrementSaturate:
        return D3D11_STENCIL_OP_DECR_SAT;
    case StencilOp::Invert:
        return D3D11_STENCIL_OP_INVERT;
    case StencilOp::IncrementWrap:
        return D3D11_STENCIL_OP_INCR;
    case StencilOp::DecrementWrap:
        return D3D11_STENCIL_OP_DECR;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid StencilOp value");
    return D3D11_STENCIL_OP(0);
}

D3D11_FILL_MODE translateFillMode(FillMode mode)
{
    switch (mode)
    {
    case FillMode::Solid:
        return D3D11_FILL_SOLID;
    case FillMode::Wireframe:
        return D3D11_FILL_WIREFRAME;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid FillMode value");
    return D3D11_FILL_MODE(0);
}

D3D11_CULL_MODE translateCullMode(CullMode mode)
{
    switch (mode)
    {
    case CullMode::None:
        return D3D11_CULL_NONE;
    case CullMode::Back:
        return D3D11_CULL_BACK;
    case CullMode::Front:
        return D3D11_CULL_FRONT;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid CullMode value");
    return D3D11_CULL_MODE(0);
}

bool isBlendDisabled(const AspectBlendDesc& desc)
{
    return desc.op == BlendOp::Add && desc.srcFactor == BlendFactor::One && desc.dstFactor == BlendFactor::Zero;
}

bool isBlendDisabled(const ColorTargetDesc& desc)
{
    return isBlendDisabled(desc.color) && isBlendDisabled(desc.alpha);
}

D3D11_BLEND_OP translateBlendOp(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add:
        return D3D11_BLEND_OP_ADD;
    case BlendOp::Subtract:
        return D3D11_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract:
        return D3D11_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min:
        return D3D11_BLEND_OP_MIN;
    case BlendOp::Max:
        return D3D11_BLEND_OP_MAX;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid BlendOp value");
    return D3D11_BLEND_OP(0);
}

D3D11_BLEND translateBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
    case BlendFactor::Zero:
        return D3D11_BLEND_ZERO;
    case BlendFactor::One:
        return D3D11_BLEND_ONE;
    case BlendFactor::SrcColor:
        return D3D11_BLEND_SRC_COLOR;
    case BlendFactor::InvSrcColor:
        return D3D11_BLEND_INV_SRC_COLOR;
    case BlendFactor::SrcAlpha:
        return D3D11_BLEND_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:
        return D3D11_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DestAlpha:
        return D3D11_BLEND_DEST_ALPHA;
    case BlendFactor::InvDestAlpha:
        return D3D11_BLEND_INV_DEST_ALPHA;
    case BlendFactor::DestColor:
        return D3D11_BLEND_DEST_COLOR;
    case BlendFactor::InvDestColor:
        return D3D11_BLEND_INV_DEST_COLOR;
    case BlendFactor::SrcAlphaSaturate:
        return D3D11_BLEND_SRC_ALPHA_SAT;
    case BlendFactor::BlendColor:
        return D3D11_BLEND_BLEND_FACTOR;
    case BlendFactor::InvBlendColor:
        return D3D11_BLEND_INV_BLEND_FACTOR;
    case BlendFactor::SecondarySrcColor:
        return D3D11_BLEND_SRC1_COLOR;
    case BlendFactor::InvSecondarySrcColor:
        return D3D11_BLEND_INV_SRC1_COLOR;
    case BlendFactor::SecondarySrcAlpha:
        return D3D11_BLEND_SRC1_ALPHA;
    case BlendFactor::InvSecondarySrcAlpha:
        return D3D11_BLEND_INV_SRC1_ALPHA;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid BlendFactor value");
    return D3D11_BLEND(0);
}

} // namespace rhi::d3d11

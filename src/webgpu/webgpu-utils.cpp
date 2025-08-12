#include "webgpu-utils.h"

#include "core/assert.h"

namespace rhi::webgpu {

WebGPUTextureFormat translateTextureFormat(Format format)
{
    switch (format)
    {
    case Format::Undefined:
        return WebGPUTextureFormat_Undefined;

    case Format::R8Uint:
        return WebGPUTextureFormat_R8Uint;
    case Format::R8Sint:
        return WebGPUTextureFormat_R8Sint;
    case Format::R8Unorm:
        return WebGPUTextureFormat_R8Unorm;
    case Format::R8Snorm:
        return WebGPUTextureFormat_R8Snorm;

    case Format::RG8Uint:
        return WebGPUTextureFormat_RG8Uint;
    case Format::RG8Sint:
        return WebGPUTextureFormat_RG8Sint;
    case Format::RG8Unorm:
        return WebGPUTextureFormat_RG8Unorm;
    case Format::RG8Snorm:
        return WebGPUTextureFormat_RG8Snorm;

    case Format::RGBA8Uint:
        return WebGPUTextureFormat_RGBA8Uint;
    case Format::RGBA8Sint:
        return WebGPUTextureFormat_RGBA8Sint;
    case Format::RGBA8Unorm:
        return WebGPUTextureFormat_RGBA8Unorm;
    case Format::RGBA8UnormSrgb:
        return WebGPUTextureFormat_RGBA8UnormSrgb;
    case Format::RGBA8Snorm:
        return WebGPUTextureFormat_RGBA8Snorm;

    case Format::BGRA8Unorm:
        return WebGPUTextureFormat_BGRA8Unorm;
    case Format::BGRA8UnormSrgb:
        return WebGPUTextureFormat_BGRA8UnormSrgb;
    case Format::BGRX8Unorm:
        return WebGPUTextureFormat_Undefined; // not supported
    case Format::BGRX8UnormSrgb:
        return WebGPUTextureFormat_Undefined; // not supported

    case Format::R16Uint:
        return WebGPUTextureFormat_R16Uint;
    case Format::R16Sint:
        return WebGPUTextureFormat_R16Sint;
    case Format::R16Unorm:
        return WebGPUTextureFormat_R16Unorm;
    case Format::R16Snorm:
        return WebGPUTextureFormat_R16Snorm;
    case Format::R16Float:
        return WebGPUTextureFormat_R16Float;

    case Format::RG16Uint:
        return WebGPUTextureFormat_RG16Uint;
    case Format::RG16Sint:
        return WebGPUTextureFormat_RG16Sint;
    case Format::RG16Unorm:
        return WebGPUTextureFormat_RG16Unorm;
    case Format::RG16Snorm:
        return WebGPUTextureFormat_RG16Snorm;
    case Format::RG16Float:
        return WebGPUTextureFormat_RG16Float;

    case Format::RGBA16Uint:
        return WebGPUTextureFormat_RGBA16Uint;
    case Format::RGBA16Sint:
        return WebGPUTextureFormat_RGBA16Sint;
    case Format::RGBA16Unorm:
        return WebGPUTextureFormat_RGBA16Unorm;
    case Format::RGBA16Snorm:
        return WebGPUTextureFormat_RGBA16Snorm;
    case Format::RGBA16Float:
        return WebGPUTextureFormat_RGBA16Float;

    case Format::R32Uint:
        return WebGPUTextureFormat_R32Uint;
    case Format::R32Sint:
        return WebGPUTextureFormat_R32Sint;
    case Format::R32Float:
        return WebGPUTextureFormat_R32Float;

    case Format::RG32Uint:
        return WebGPUTextureFormat_RG32Uint;
    case Format::RG32Sint:
        return WebGPUTextureFormat_RG32Sint;
    case Format::RG32Float:
        return WebGPUTextureFormat_RG32Float;

    case Format::RGB32Uint:
        return WebGPUTextureFormat_Undefined; // not supported
    case Format::RGB32Sint:
        return WebGPUTextureFormat_Undefined; // not supported
    case Format::RGB32Float:
        return WebGPUTextureFormat_Undefined; // not supported

    case Format::RGBA32Uint:
        return WebGPUTextureFormat_RGBA32Uint;
    case Format::RGBA32Sint:
        return WebGPUTextureFormat_RGBA32Sint;
    case Format::RGBA32Float:
        return WebGPUTextureFormat_RGBA32Float;

    case Format::R64Uint:
        return WebGPUTextureFormat_Undefined; // not supported
    case Format::R64Sint:
        return WebGPUTextureFormat_Undefined; // not supported

    case Format::BGRA4Unorm:
        return WebGPUTextureFormat_Undefined; // not supported
    case Format::B5G6R5Unorm:
        return WebGPUTextureFormat_Undefined; // not supported
    case Format::BGR5A1Unorm:
        return WebGPUTextureFormat_Undefined; // not supported

    case Format::RGB9E5Ufloat:
        return WebGPUTextureFormat_RGB9E5Ufloat;
    case Format::RGB10A2Uint:
        return WebGPUTextureFormat_RGB10A2Uint;
    case Format::RGB10A2Unorm:
        return WebGPUTextureFormat_RGB10A2Unorm;
    case Format::R11G11B10Float:
        return WebGPUTextureFormat_RG11B10Ufloat;

    case Format::D32Float:
        return WebGPUTextureFormat_Depth32Float;
    case Format::D16Unorm:
        return WebGPUTextureFormat_Depth16Unorm;
    case Format::D32FloatS8Uint:
        return WebGPUTextureFormat_Depth32FloatStencil8;

    case Format::BC1Unorm:
        return WebGPUTextureFormat_BC1RGBAUnorm;
    case Format::BC1UnormSrgb:
        return WebGPUTextureFormat_BC1RGBAUnormSrgb;
    case Format::BC2Unorm:
        return WebGPUTextureFormat_BC2RGBAUnorm;
    case Format::BC2UnormSrgb:
        return WebGPUTextureFormat_BC2RGBAUnormSrgb;
    case Format::BC3Unorm:
        return WebGPUTextureFormat_BC3RGBAUnorm;
    case Format::BC3UnormSrgb:
        return WebGPUTextureFormat_BC3RGBAUnormSrgb;
    case Format::BC4Unorm:
        return WebGPUTextureFormat_BC4RUnorm;
    case Format::BC4Snorm:
        return WebGPUTextureFormat_BC4RSnorm;
    case Format::BC5Unorm:
        return WebGPUTextureFormat_BC5RGUnorm;
    case Format::BC5Snorm:
        return WebGPUTextureFormat_BC5RGSnorm;
    case Format::BC6HUfloat:
        return WebGPUTextureFormat_BC6HRGBUfloat;
    case Format::BC6HSfloat:
        return WebGPUTextureFormat_BC6HRGBFloat;
    case Format::BC7Unorm:
        return WebGPUTextureFormat_BC7RGBAUnorm;
    case Format::BC7UnormSrgb:
        return WebGPUTextureFormat_BC7RGBAUnormSrgb;

    default:
        return WebGPUTextureFormat_Undefined;
    }
}

WebGPUVertexFormat translateVertexFormat(Format format)
{
    switch (format)
    {
    case Format::RG8Uint:
        return WebGPUVertexFormat_Uint8x2;
    case Format::RG8Sint:
        return WebGPUVertexFormat_Sint8x2;
    case Format::RG8Unorm:
        return WebGPUVertexFormat_Unorm8x2;
    case Format::RG8Snorm:
        return WebGPUVertexFormat_Snorm8x2;

    case Format::RGBA8Uint:
        return WebGPUVertexFormat_Uint8x4;
    case Format::RGBA8Sint:
        return WebGPUVertexFormat_Sint8x4;
    case Format::RGBA8Unorm:
        return WebGPUVertexFormat_Unorm8x4;
    case Format::RGBA8Snorm:
        return WebGPUVertexFormat_Snorm8x4;

    case Format::RG16Uint:
        return WebGPUVertexFormat_Uint16x2;
    case Format::RG16Sint:
        return WebGPUVertexFormat_Sint16x2;
    case Format::RG16Unorm:
        return WebGPUVertexFormat_Unorm16x2;
    case Format::RG16Snorm:
        return WebGPUVertexFormat_Snorm16x2;
    case Format::RG16Float:
        return WebGPUVertexFormat_Float16x2;

    case Format::RGBA16Uint:
        return WebGPUVertexFormat_Uint16x4;
    case Format::RGBA16Sint:
        return WebGPUVertexFormat_Sint16x4;
    case Format::RGBA16Unorm:
        return WebGPUVertexFormat_Unorm16x4;
    case Format::RGBA16Snorm:
        return WebGPUVertexFormat_Snorm16x4;
    case Format::RGBA16Float:
        return WebGPUVertexFormat_Float16x4;

    case Format::R32Uint:
        return WebGPUVertexFormat_Uint32;
    case Format::R32Sint:
        return WebGPUVertexFormat_Sint32;
    case Format::R32Float:
        return WebGPUVertexFormat_Float32;

    case Format::RG32Uint:
        return WebGPUVertexFormat_Uint32x2;
    case Format::RG32Sint:
        return WebGPUVertexFormat_Sint32x2;
    case Format::RG32Float:
        return WebGPUVertexFormat_Float32x2;

    case Format::RGB32Uint:
        return WebGPUVertexFormat_Uint32x3;
    case Format::RGB32Sint:
        return WebGPUVertexFormat_Sint32x3;
    case Format::RGB32Float:
        return WebGPUVertexFormat_Float32x3;

    case Format::RGBA32Uint:
        return WebGPUVertexFormat_Uint32x4;
    case Format::RGBA32Sint:
        return WebGPUVertexFormat_Sint32x4;
    case Format::RGBA32Float:
        return WebGPUVertexFormat_Float32x4;

    default:
        return WebGPUVertexFormat(0);
    }
}

WebGPUBufferUsage translateBufferUsage(BufferUsage usage)
{
    WebGPUBufferUsage result = WebGPUBufferUsage_None;
    if (is_set(usage, BufferUsage::VertexBuffer))
        result |= WebGPUBufferUsage_Vertex;
    if (is_set(usage, BufferUsage::IndexBuffer))
        result |= WebGPUBufferUsage_Index;
    if (is_set(usage, BufferUsage::ConstantBuffer))
        result |= WebGPUBufferUsage_Uniform;
    if (is_set(usage, BufferUsage::ShaderResource))
        result |= WebGPUBufferUsage_Storage;
    if (is_set(usage, BufferUsage::UnorderedAccess))
        result |= WebGPUBufferUsage_Storage;
    if (is_set(usage, BufferUsage::IndirectArgument))
        result |= WebGPUBufferUsage_Indirect;
    if (is_set(usage, BufferUsage::CopySource))
        result |= WebGPUBufferUsage_CopySrc;
    if (is_set(usage, BufferUsage::CopyDestination))
        result |= WebGPUBufferUsage_CopyDst;
    return result;
}

WebGPUTextureUsage translateTextureUsage(TextureUsage usage)
{
    WebGPUTextureUsage result = WebGPUTextureUsage_None;
    if (is_set(usage, TextureUsage::ShaderResource))
        result |= WebGPUTextureUsage_TextureBinding;
    if (is_set(usage, TextureUsage::UnorderedAccess))
        result |= WebGPUTextureUsage_StorageBinding;
    if (is_set(usage, TextureUsage::RenderTarget))
        result |= WebGPUTextureUsage_RenderAttachment;
    if (is_set(usage, TextureUsage::DepthStencil))
        result |= WebGPUTextureUsage_RenderAttachment;
    if (is_set(usage, TextureUsage::CopySource))
        result |= WebGPUTextureUsage_CopySrc;
    if (is_set(usage, TextureUsage::CopyDestination))
        result |= WebGPUTextureUsage_CopyDst;
    if (is_set(usage, TextureUsage::ResolveSource))
        result |= WebGPUTextureUsage_CopySrc;
    if (is_set(usage, TextureUsage::ResolveDestination))
        result |= WebGPUTextureUsage_CopyDst;
    return result;
}

WebGPUTextureViewDimension translateTextureViewDimension(TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1D:
        return WebGPUTextureViewDimension_1D;
    case TextureType::Texture1DArray:
        return WebGPUTextureViewDimension_Undefined;
    case TextureType::Texture2D:
    case TextureType::Texture2DMS:
        return WebGPUTextureViewDimension_2D;
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMSArray:
        return WebGPUTextureViewDimension_2DArray;
    case TextureType::TextureCube:
        return WebGPUTextureViewDimension_Cube;
    case TextureType::TextureCubeArray:
        return WebGPUTextureViewDimension_CubeArray;
    case TextureType::Texture3D:
        return WebGPUTextureViewDimension_3D;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureType value");
    return WebGPUTextureViewDimension_Undefined;
}

WebGPUTextureAspect translateTextureAspect(TextureAspect aspect)
{
    switch (aspect)
    {
    case TextureAspect::All:
        return WebGPUTextureAspect_All;
    case TextureAspect::DepthOnly:
        return WebGPUTextureAspect_DepthOnly;
    case TextureAspect::StencilOnly:
        return WebGPUTextureAspect_StencilOnly;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureAspect value");
    return WebGPUTextureAspect_Undefined;
}

WebGPUAddressMode translateAddressMode(TextureAddressingMode mode)
{
    switch (mode)
    {
    case TextureAddressingMode::Wrap:
        return WebGPUAddressMode_Repeat;
    case TextureAddressingMode::ClampToEdge:
        return WebGPUAddressMode_ClampToEdge;
    case TextureAddressingMode::ClampToBorder:
        return WebGPUAddressMode_Undefined; // not supported (warn in validation)
    case TextureAddressingMode::MirrorRepeat:
        return WebGPUAddressMode_MirrorRepeat;
    case TextureAddressingMode::MirrorOnce:
        return WebGPUAddressMode_Undefined; // not supported (warn in validation)
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureAddressingMode value");
    return WebGPUAddressMode_Repeat;
}

WebGPUFilterMode translateFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return WebGPUFilterMode_Nearest;
    case TextureFilteringMode::Linear:
        return WebGPUFilterMode_Linear;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureFilteringMode value");
    return WebGPUFilterMode_Undefined;
}

WebGPUMipmapFilterMode translateMipmapFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return WebGPUMipmapFilterMode_Nearest;
    case TextureFilteringMode::Linear:
        return WebGPUMipmapFilterMode_Linear;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureFilteringMode value");
    return WebGPUMipmapFilterMode_Undefined;
}

WebGPUCompareFunction translateCompareFunction(ComparisonFunc func)
{
    switch (func)
    {
    case ComparisonFunc::Never:
        return WebGPUCompareFunction_Never;
    case ComparisonFunc::Less:
        return WebGPUCompareFunction_Less;
    case ComparisonFunc::Equal:
        return WebGPUCompareFunction_Equal;
    case ComparisonFunc::LessEqual:
        return WebGPUCompareFunction_LessEqual;
    case ComparisonFunc::Greater:
        return WebGPUCompareFunction_Greater;
    case ComparisonFunc::NotEqual:
        return WebGPUCompareFunction_NotEqual;
    case ComparisonFunc::GreaterEqual:
        return WebGPUCompareFunction_GreaterEqual;
    case ComparisonFunc::Always:
        return WebGPUCompareFunction_Always;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid ComparisonFunc value");
    return WebGPUCompareFunction_Undefined;
}

WebGPUPrimitiveTopology translatePrimitiveTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::PointList:
        return WebGPUPrimitiveTopology_PointList;
    case PrimitiveTopology::LineList:
        return WebGPUPrimitiveTopology_LineList;
    case PrimitiveTopology::LineStrip:
        return WebGPUPrimitiveTopology_LineStrip;
    case PrimitiveTopology::TriangleList:
        return WebGPUPrimitiveTopology_TriangleList;
    case PrimitiveTopology::TriangleStrip:
        return WebGPUPrimitiveTopology_TriangleStrip;
    case PrimitiveTopology::PatchList:
        return WebGPUPrimitiveTopology_Undefined; // not supported (warn in validation)
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid PrimitiveTopology value");
    return WebGPUPrimitiveTopology_Undefined;
}

WebGPUFrontFace translateFrontFace(FrontFaceMode mode)
{
    switch (mode)
    {
    case FrontFaceMode::CounterClockwise:
        return WebGPUFrontFace_CCW;
    case FrontFaceMode::Clockwise:
        return WebGPUFrontFace_CW;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid FrontFaceMode value");
    return WebGPUFrontFace_Undefined;
}

WebGPUCullMode translateCullMode(CullMode mode)
{
    switch (mode)
    {
    case CullMode::None:
        return WebGPUCullMode_None;
    case CullMode::Front:
        return WebGPUCullMode_Front;
    case CullMode::Back:
        return WebGPUCullMode_Back;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid CullMode value");
    return WebGPUCullMode_Undefined;
}

WebGPUStencilOperation translateStencilOp(StencilOp op)
{
    switch (op)
    {
    case StencilOp::Keep:
        return WebGPUStencilOperation_Keep;
    case StencilOp::Zero:
        return WebGPUStencilOperation_Zero;
    case StencilOp::Replace:
        return WebGPUStencilOperation_Replace;
    case StencilOp::IncrementSaturate:
        return WebGPUStencilOperation_IncrementClamp;
    case StencilOp::DecrementSaturate:
        return WebGPUStencilOperation_DecrementClamp;
    case StencilOp::Invert:
        return WebGPUStencilOperation_Invert;
    case StencilOp::IncrementWrap:
        return WebGPUStencilOperation_IncrementWrap;
    case StencilOp::DecrementWrap:
        return WebGPUStencilOperation_DecrementWrap;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid StencilOp value");
    return WebGPUStencilOperation_Undefined;
}

WebGPUBlendFactor translateBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
    case BlendFactor::Zero:
        return WebGPUBlendFactor_Zero;
    case BlendFactor::One:
        return WebGPUBlendFactor_One;
    case BlendFactor::SrcColor:
        return WebGPUBlendFactor_Src;
    case BlendFactor::InvSrcColor:
        return WebGPUBlendFactor_OneMinusSrc;
    case BlendFactor::SrcAlpha:
        return WebGPUBlendFactor_SrcAlpha;
    case BlendFactor::InvSrcAlpha:
        return WebGPUBlendFactor_OneMinusSrcAlpha;
    case BlendFactor::DestAlpha:
        return WebGPUBlendFactor_DstAlpha;
    case BlendFactor::InvDestAlpha:
        return WebGPUBlendFactor_OneMinusDstAlpha;
    case BlendFactor::DestColor:
        return WebGPUBlendFactor_Dst;
    case BlendFactor::InvDestColor:
        return WebGPUBlendFactor_OneMinusDst;
    case BlendFactor::SrcAlphaSaturate:
        return WebGPUBlendFactor_SrcAlphaSaturated;
    case BlendFactor::BlendColor:
        return WebGPUBlendFactor_Constant;
    case BlendFactor::InvBlendColor:
        return WebGPUBlendFactor_OneMinusConstant;
    case BlendFactor::SecondarySrcColor:
        return WebGPUBlendFactor_Src1;
    case BlendFactor::InvSecondarySrcColor:
        return WebGPUBlendFactor_OneMinusSrc1;
    case BlendFactor::SecondarySrcAlpha:
        return WebGPUBlendFactor_Src1Alpha;
    case BlendFactor::InvSecondarySrcAlpha:
        return WebGPUBlendFactor_OneMinusSrc1Alpha;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid BlendFactor value");
    return WebGPUBlendFactor_Undefined;
}

WebGPUBlendOperation translateBlendOperation(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add:
        return WebGPUBlendOperation_Add;
    case BlendOp::Subtract:
        return WebGPUBlendOperation_Subtract;
    case BlendOp::ReverseSubtract:
        return WebGPUBlendOperation_ReverseSubtract;
    case BlendOp::Min:
        return WebGPUBlendOperation_Min;
    case BlendOp::Max:
        return WebGPUBlendOperation_Max;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid BlendOp value");
    return WebGPUBlendOperation_Undefined;
}

WebGPULoadOp translateLoadOp(LoadOp op)
{
    switch (op)
    {
    case LoadOp::Load:
        return WebGPULoadOp_Load;
    case LoadOp::Clear:
        return WebGPULoadOp_Clear;
    case LoadOp::DontCare:
        return WebGPULoadOp_Undefined;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid LoadOp value");
    return WebGPULoadOp_Undefined;
}

WebGPUStoreOp translateStoreOp(StoreOp op)
{
    switch (op)
    {
    case StoreOp::Store:
        return WebGPUStoreOp_Store;
    case StoreOp::DontCare:
        return WebGPUStoreOp_Undefined;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid StoreOp value");
    return WebGPUStoreOp_Undefined;
}

} // namespace rhi::webgpu

#include "wgpu-util.h"

#include "core/assert.h"

namespace rhi::wgpu {

WGPUTextureFormat translateTextureFormat(Format format)
{
    switch (format)
    {
    case Format::Unknown:
        return WGPUTextureFormat_Undefined;

    case Format::R32G32B32A32_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R32G32B32_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R32G32_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R32_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported

    case Format::R16G16B16A16_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R16G16_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R16_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported

    case Format::R8G8B8A8_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R8G8_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R8_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::B8G8R8A8_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported

    case Format::R32G32B32A32_FLOAT:
        return WGPUTextureFormat_RGBA32Float;
    case Format::R32G32B32_FLOAT:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R32G32_FLOAT:
        return WGPUTextureFormat_RG32Float;
    case Format::R32_FLOAT:
        return WGPUTextureFormat_R32Float;

    case Format::R16G16B16A16_FLOAT:
        return WGPUTextureFormat_RGBA16Float;
    case Format::R16G16_FLOAT:
        return WGPUTextureFormat_RG16Float;
    case Format::R16_FLOAT:
        return WGPUTextureFormat_R16Float;

    case Format::R32G32B32A32_UINT:
        return WGPUTextureFormat_RGBA32Uint;
    case Format::R32G32B32_UINT:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R32G32_UINT:
        return WGPUTextureFormat_RG32Uint;
    case Format::R32_UINT:
        return WGPUTextureFormat_R32Uint;

    case Format::R16G16B16A16_UINT:
        return WGPUTextureFormat_RGBA16Uint;
    case Format::R16G16_UINT:
        return WGPUTextureFormat_RG16Uint;
    case Format::R16_UINT:
        return WGPUTextureFormat_R16Uint;

    case Format::R8G8B8A8_UINT:
        return WGPUTextureFormat_RGBA8Uint;
    case Format::R8G8_UINT:
        return WGPUTextureFormat_RG8Uint;
    case Format::R8_UINT:
        return WGPUTextureFormat_R8Uint;

    case Format::R32G32B32A32_SINT:
        return WGPUTextureFormat_RGBA32Sint;
    case Format::R32G32B32_SINT:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R32G32_SINT:
        return WGPUTextureFormat_RG32Sint;
    case Format::R32_SINT:
        return WGPUTextureFormat_R32Sint;

    case Format::R16G16B16A16_SINT:
        return WGPUTextureFormat_RGBA16Sint;
    case Format::R16G16_SINT:
        return WGPUTextureFormat_RG16Sint;
    case Format::R16_SINT:
        return WGPUTextureFormat_R16Sint;

    case Format::R8G8B8A8_SINT:
        return WGPUTextureFormat_RGBA8Sint;
    case Format::R8G8_SINT:
        return WGPUTextureFormat_RG8Sint;
    case Format::R8_SINT:
        return WGPUTextureFormat_R8Sint;

    case Format::R16G16B16A16_UNORM:
        return WGPUTextureFormat_RGBA16Unorm;
    case Format::R16G16_UNORM:
        return WGPUTextureFormat_RG16Unorm;
    case Format::R16_UNORM:
        return WGPUTextureFormat_R16Unorm;

    case Format::R8G8B8A8_UNORM:
        return WGPUTextureFormat_RGBA8Unorm;
    case Format::R8G8B8A8_UNORM_SRGB:
        return WGPUTextureFormat_RGBA8UnormSrgb;
    case Format::R8G8_UNORM:
        return WGPUTextureFormat_RG8Unorm;
    case Format::R8_UNORM:
        return WGPUTextureFormat_R8Unorm;
    case Format::B8G8R8A8_UNORM:
        return WGPUTextureFormat_BGRA8Unorm;
    case Format::B8G8R8A8_UNORM_SRGB:
        return WGPUTextureFormat_BGRA8UnormSrgb;
    case Format::B8G8R8X8_UNORM:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::B8G8R8X8_UNORM_SRGB:
        return WGPUTextureFormat_Undefined; // not supported

    case Format::R16G16B16A16_SNORM:
        return WGPUTextureFormat_RGBA16Snorm;
    case Format::R16G16_SNORM:
        return WGPUTextureFormat_RG16Snorm;
    case Format::R16_SNORM:
        return WGPUTextureFormat_R16Snorm;

    case Format::R8G8B8A8_SNORM:
        return WGPUTextureFormat_RGBA8Snorm;
    case Format::R8G8_SNORM:
        return WGPUTextureFormat_RG8Snorm;
    case Format::R8_SNORM:
        return WGPUTextureFormat_R8Snorm;

    case Format::D32_FLOAT:
        return WGPUTextureFormat_Depth32Float;
    case Format::D16_UNORM:
        return WGPUTextureFormat_Depth16Unorm;
    case Format::D32_FLOAT_S8_UINT:
        return WGPUTextureFormat_Depth32FloatStencil8;
    case Format::R32_FLOAT_X32_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported

    case Format::B4G4R4A4_UNORM:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::B5G6R5_UNORM:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::B5G5R5A1_UNORM:
        return WGPUTextureFormat_Undefined; // not supported

    case Format::R9G9B9E5_SHAREDEXP:
        return WGPUTextureFormat_RGB9E5Ufloat;
    case Format::R10G10B10A2_TYPELESS:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R10G10B10A2_UNORM:
        return WGPUTextureFormat_RGB10A2Unorm;
    case Format::R10G10B10A2_UINT:
        return WGPUTextureFormat_RGB10A2Uint;
    case Format::R11G11B10_FLOAT:
        return WGPUTextureFormat_RG11B10Ufloat;

    case Format::BC1_UNORM:
        return WGPUTextureFormat_BC1RGBAUnorm;
    case Format::BC1_UNORM_SRGB:
        return WGPUTextureFormat_BC1RGBAUnormSrgb;
    case Format::BC2_UNORM:
        return WGPUTextureFormat_BC2RGBAUnorm;
    case Format::BC2_UNORM_SRGB:
        return WGPUTextureFormat_BC2RGBAUnormSrgb;
    case Format::BC3_UNORM:
        return WGPUTextureFormat_BC3RGBAUnorm;
    case Format::BC3_UNORM_SRGB:
        return WGPUTextureFormat_BC3RGBAUnormSrgb;
    case Format::BC4_UNORM:
        return WGPUTextureFormat_BC4RUnorm;
    case Format::BC4_SNORM:
        return WGPUTextureFormat_BC4RSnorm;
    case Format::BC5_UNORM:
        return WGPUTextureFormat_BC5RGUnorm;
    case Format::BC5_SNORM:
        return WGPUTextureFormat_BC5RGSnorm;
    case Format::BC6H_UF16:
        return WGPUTextureFormat_BC6HRGBUfloat;
    case Format::BC6H_SF16:
        return WGPUTextureFormat_BC6HRGBFloat;
    case Format::BC7_UNORM:
        return WGPUTextureFormat_BC7RGBAUnorm;
    case Format::BC7_UNORM_SRGB:
        return WGPUTextureFormat_BC7RGBAUnormSrgb;

    case Format::R64_UINT:
        return WGPUTextureFormat_Undefined;
    case Format::R64_SINT:
        return WGPUTextureFormat_Undefined;

    default:
        return WGPUTextureFormat_Undefined;
    }
}

WGPUVertexFormat translateVertexFormat(Format format)
{
    switch (format)
    {
    case Format::R8G8_UINT:
        return WGPUVertexFormat_Uint8x2;
    case Format::R8G8B8A8_UINT:
        return WGPUVertexFormat_Uint8x4;

    case Format::R8G8_SINT:
        return WGPUVertexFormat_Sint8x2;
    case Format::R8G8B8A8_SINT:
        return WGPUVertexFormat_Sint8x4;

    case Format::R8G8_UNORM:
        return WGPUVertexFormat_Unorm8x2;
    case Format::R8G8B8A8_UNORM:
        return WGPUVertexFormat_Unorm8x4;

    case Format::R8G8_SNORM:
        return WGPUVertexFormat_Snorm8x2;
    case Format::R8G8B8A8_SNORM:
        return WGPUVertexFormat_Snorm8x4;

    case Format::R16G16_UINT:
        return WGPUVertexFormat_Uint16x2;
    case Format::R16G16B16A16_UINT:
        return WGPUVertexFormat_Uint16x4;

    case Format::R16G16_SINT:
        return WGPUVertexFormat_Sint16x2;
    case Format::R16G16B16A16_SINT:
        return WGPUVertexFormat_Sint16x4;

    case Format::R16G16_UNORM:
        return WGPUVertexFormat_Unorm16x2;
    case Format::R16G16B16A16_UNORM:
        return WGPUVertexFormat_Unorm16x4;

    case Format::R16G16_SNORM:
        return WGPUVertexFormat_Snorm16x2;
    case Format::R16G16B16A16_SNORM:
        return WGPUVertexFormat_Snorm16x4;

    case Format::R16G16_FLOAT:
        return WGPUVertexFormat_Float16x2;
    case Format::R16G16B16A16_FLOAT:
        return WGPUVertexFormat_Float16x4;

    case Format::R32_FLOAT:
        return WGPUVertexFormat_Float32;
    case Format::R32G32_FLOAT:
        return WGPUVertexFormat_Float32x2;
    case Format::R32G32B32_FLOAT:
        return WGPUVertexFormat_Float32x3;
    case Format::R32G32B32A32_FLOAT:
        return WGPUVertexFormat_Float32x4;

    case Format::R32_UINT:
        return WGPUVertexFormat_Uint32;
    case Format::R32G32_UINT:
        return WGPUVertexFormat_Uint32x2;
    case Format::R32G32B32_UINT:
        return WGPUVertexFormat_Uint32x3;
    case Format::R32G32B32A32_UINT:
        return WGPUVertexFormat_Uint32x4;

    case Format::R32_SINT:
        return WGPUVertexFormat_Sint32;
    case Format::R32G32_SINT:
        return WGPUVertexFormat_Sint32x2;
    case Format::R32G32B32_SINT:
        return WGPUVertexFormat_Sint32x3;
    case Format::R32G32B32A32_SINT:
        return WGPUVertexFormat_Sint32x4;

    default:
        return WGPUVertexFormat(0);
    }
}

WGPUBufferUsage translateBufferUsage(BufferUsage usage)
{
    WGPUBufferUsage result = WGPUBufferUsage_None;
    if (is_set(usage, BufferUsage::VertexBuffer))
        result |= WGPUBufferUsage_Vertex;
    if (is_set(usage, BufferUsage::IndexBuffer))
        result |= WGPUBufferUsage_Index;
    if (is_set(usage, BufferUsage::ConstantBuffer))
        result |= WGPUBufferUsage_Uniform;
    if (is_set(usage, BufferUsage::ShaderResource))
        result |= WGPUBufferUsage_Storage;
    if (is_set(usage, BufferUsage::UnorderedAccess))
        result |= WGPUBufferUsage_Storage;
    if (is_set(usage, BufferUsage::IndirectArgument))
        result |= WGPUBufferUsage_Indirect;
    if (is_set(usage, BufferUsage::CopySource))
        result |= WGPUBufferUsage_CopySrc;
    if (is_set(usage, BufferUsage::CopyDestination))
        result |= WGPUBufferUsage_CopyDst;
    return result;
}

WGPUTextureUsage translateTextureUsage(TextureUsage usage)
{
    WGPUTextureUsage result = WGPUTextureUsage_None;
    if (is_set(usage, TextureUsage::ShaderResource))
        result |= WGPUTextureUsage_TextureBinding;
    if (is_set(usage, TextureUsage::UnorderedAccess))
        result |= WGPUTextureUsage_StorageBinding;
    if (is_set(usage, TextureUsage::RenderTarget))
        result |= WGPUTextureUsage_RenderAttachment;
    if (is_set(usage, TextureUsage::DepthRead))
        result |= WGPUTextureUsage_RenderAttachment;
    if (is_set(usage, TextureUsage::DepthWrite))
        result |= WGPUTextureUsage_RenderAttachment;
    if (is_set(usage, TextureUsage::CopySource))
        result |= WGPUTextureUsage_CopySrc;
    if (is_set(usage, TextureUsage::CopyDestination))
        result |= WGPUTextureUsage_CopyDst;
    if (is_set(usage, TextureUsage::ResolveSource))
        result |= WGPUTextureUsage_CopySrc;
    if (is_set(usage, TextureUsage::ResolveDestination))
        result |= WGPUTextureUsage_CopyDst;
    return result;
}

WGPUTextureDimension translateTextureDimension(TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1D:
        return WGPUTextureDimension_1D;
    case TextureType::Texture2D:
        return WGPUTextureDimension_2D;
    case TextureType::Texture3D:
        return WGPUTextureDimension_3D;
    case TextureType::TextureCube:
        return WGPUTextureDimension_2D;
    default:
        return WGPUTextureDimension_Undefined;
    }
}

WGPUTextureViewDimension translateTextureViewDimension(TextureType type, bool array)
{
    switch (type)
    {
    case TextureType::Texture1D:
        SLANG_RHI_ASSERT(!array);
        return WGPUTextureViewDimension_1D;
    case TextureType::Texture2D:
        return array ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
    case TextureType::Texture3D:
        SLANG_RHI_ASSERT(!array);
        return WGPUTextureViewDimension_3D;
    case TextureType::TextureCube:
        return array ? WGPUTextureViewDimension_CubeArray : WGPUTextureViewDimension_Cube;
    default:
        return WGPUTextureViewDimension_Undefined;
    }
}

WGPUTextureAspect translateTextureAspect(TextureAspect aspect)
{
    switch (aspect)
    {
    case TextureAspect::All:
        return WGPUTextureAspect_All;
    case TextureAspect::DepthOnly:
        return WGPUTextureAspect_DepthOnly;
    case TextureAspect::StencilOnly:
        return WGPUTextureAspect_StencilOnly;
    default:
        return WGPUTextureAspect_All;
    }
}

WGPUAddressMode translateAddressMode(TextureAddressingMode mode)
{
    switch (mode)
    {
    case TextureAddressingMode::Wrap:
        return WGPUAddressMode_Repeat;
    case TextureAddressingMode::ClampToEdge:
        return WGPUAddressMode_ClampToEdge;
    case TextureAddressingMode::ClampToBorder:
        return WGPUAddressMode_ClampToEdge; // TODO not supported (warn in validation)
    case TextureAddressingMode::MirrorRepeat:
        return WGPUAddressMode_MirrorRepeat;
    case TextureAddressingMode::MirrorOnce:
        return WGPUAddressMode_MirrorRepeat; // TODO not supported (warn in validation)
    default:
        return WGPUAddressMode_Repeat;
    }
}

WGPUFilterMode translateFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return WGPUFilterMode_Nearest;
    case TextureFilteringMode::Linear:
        return WGPUFilterMode_Linear;
    default:
        return WGPUFilterMode_Nearest;
    }
}

WGPUMipmapFilterMode translateMipmapFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return WGPUMipmapFilterMode_Nearest;
    case TextureFilteringMode::Linear:
        return WGPUMipmapFilterMode_Linear;
    default:
        return WGPUMipmapFilterMode_Nearest;
    }
}

WGPUCompareFunction translateCompareFunction(ComparisonFunc func)
{
    switch (func)
    {
    case ComparisonFunc::Never:
        return WGPUCompareFunction_Never;
    case ComparisonFunc::Less:
        return WGPUCompareFunction_Less;
    case ComparisonFunc::Equal:
        return WGPUCompareFunction_Equal;
    case ComparisonFunc::LessEqual:
        return WGPUCompareFunction_LessEqual;
    case ComparisonFunc::Greater:
        return WGPUCompareFunction_Greater;
    case ComparisonFunc::NotEqual:
        return WGPUCompareFunction_NotEqual;
    case ComparisonFunc::GreaterEqual:
        return WGPUCompareFunction_GreaterEqual;
    case ComparisonFunc::Always:
        return WGPUCompareFunction_Always;
    default:
        return WGPUCompareFunction_Never;
    }
}

WGPUPrimitiveTopology translatePrimitiveTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::PointList:
        return WGPUPrimitiveTopology_PointList;
    case PrimitiveTopology::LineList:
        return WGPUPrimitiveTopology_LineList;
    case PrimitiveTopology::LineStrip:
        return WGPUPrimitiveTopology_LineStrip;
    case PrimitiveTopology::TriangleList:
        return WGPUPrimitiveTopology_TriangleList;
    case PrimitiveTopology::TriangleStrip:
        return WGPUPrimitiveTopology_TriangleStrip;
    case PrimitiveTopology::PatchList:
        SLANG_RHI_ASSERT_FAILURE("Patch tlist opology not supported.");
    default:
        return WGPUPrimitiveTopology_Undefined;
    }
}

WGPUFrontFace translateFrontFace(FrontFaceMode mode)
{
    switch (mode)
    {
    case FrontFaceMode::CounterClockwise:
        return WGPUFrontFace_CCW;
    case FrontFaceMode::Clockwise:
        return WGPUFrontFace_CW;
    default:
        return WGPUFrontFace_Undefined;
    }
}

WGPUCullMode translateCullMode(CullMode mode)
{
    switch (mode)
    {
    case CullMode::None:
        return WGPUCullMode_None;
    case CullMode::Front:
        return WGPUCullMode_Front;
    case CullMode::Back:
        return WGPUCullMode_Back;
    default:
        return WGPUCullMode_Undefined;
    }
}

WGPUStencilOperation translateStencilOp(StencilOp op)
{
    switch (op)
    {
    case StencilOp::Keep:
        return WGPUStencilOperation_Keep;
    case StencilOp::Zero:
        return WGPUStencilOperation_Zero;
    case StencilOp::Replace:
        return WGPUStencilOperation_Replace;
    case StencilOp::IncrementSaturate:
        return WGPUStencilOperation_IncrementClamp;
    case StencilOp::DecrementSaturate:
        return WGPUStencilOperation_DecrementClamp;
    case StencilOp::Invert:
        return WGPUStencilOperation_Invert;
    case StencilOp::IncrementWrap:
        return WGPUStencilOperation_IncrementWrap;
    case StencilOp::DecrementWrap:
        return WGPUStencilOperation_DecrementWrap;
    default:
        return WGPUStencilOperation_Undefined;
    }
}

WGPUBlendFactor translateBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
    case BlendFactor::Zero:
        return WGPUBlendFactor_Zero;
    case BlendFactor::One:
        return WGPUBlendFactor_One;
    case BlendFactor::SrcColor:
        return WGPUBlendFactor_Src;
    case BlendFactor::InvSrcColor:
        return WGPUBlendFactor_OneMinusSrc;
    case BlendFactor::SrcAlpha:
        return WGPUBlendFactor_SrcAlpha;
    case BlendFactor::InvSrcAlpha:
        return WGPUBlendFactor_OneMinusSrcAlpha;
    case BlendFactor::DestAlpha:
        return WGPUBlendFactor_DstAlpha;
    case BlendFactor::InvDestAlpha:
        return WGPUBlendFactor_OneMinusDstAlpha;
    case BlendFactor::DestColor:
        return WGPUBlendFactor_Dst;
    case BlendFactor::InvDestColor:
        return WGPUBlendFactor_OneMinusDst;
    case BlendFactor::SrcAlphaSaturate:
        return WGPUBlendFactor_SrcAlphaSaturated;
    case BlendFactor::BlendColor:
        return WGPUBlendFactor_Constant;
    case BlendFactor::InvBlendColor:
        return WGPUBlendFactor_OneMinusConstant;
    case BlendFactor::SecondarySrcColor:
        return WGPUBlendFactor_Src1;
    case BlendFactor::InvSecondarySrcColor:
        return WGPUBlendFactor_OneMinusSrc1;
    case BlendFactor::SecondarySrcAlpha:
        return WGPUBlendFactor_Src1Alpha;
    case BlendFactor::InvSecondarySrcAlpha:
        return WGPUBlendFactor_OneMinusSrc1Alpha;
    default:
        return WGPUBlendFactor_Undefined;
    }
}

WGPUBlendOperation translateBlendOperation(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add:
        return WGPUBlendOperation_Add;
    case BlendOp::Subtract:
        return WGPUBlendOperation_Subtract;
    case BlendOp::ReverseSubtract:
        return WGPUBlendOperation_ReverseSubtract;
    case BlendOp::Min:
        return WGPUBlendOperation_Min;
    case BlendOp::Max:
        return WGPUBlendOperation_Max;
    default:
        return WGPUBlendOperation_Undefined;
    }
}

WGPULoadOp translateLoadOp(LoadOp op)
{
    switch (op)
    {
    case LoadOp::Load:
        return WGPULoadOp_Load;
    case LoadOp::Clear:
        return WGPULoadOp_Clear;
    case LoadOp::DontCare:
        return WGPULoadOp_Undefined;
    default:
        return WGPULoadOp_Undefined;
    }
}

WGPUStoreOp translateStoreOp(StoreOp op)
{
    switch (op)
    {
    case StoreOp::Store:
        return WGPUStoreOp_Store;
    case StoreOp::DontCare:
        return WGPUStoreOp_Undefined;
    default:
        return WGPUStoreOp_Undefined;
    }
}

} // namespace rhi::wgpu

#include "wgpu-utils.h"

#include "core/assert.h"

#include <cstring>

namespace rhi::wgpu {

WGPUStringView translateString(const char* str)
{
    return str ? WGPUStringView{str, ::strlen(str)} : WGPUStringView{nullptr, 0};
}

WGPUTextureFormat translateTextureFormat(Format format)
{
    switch (format)
    {
    case Format::Undefined:
        return WGPUTextureFormat_Undefined;

    case Format::R8Uint:
        return WGPUTextureFormat_R8Uint;
    case Format::R8Sint:
        return WGPUTextureFormat_R8Sint;
    case Format::R8Unorm:
        return WGPUTextureFormat_R8Unorm;
    case Format::R8Snorm:
        return WGPUTextureFormat_R8Snorm;

    case Format::RG8Uint:
        return WGPUTextureFormat_RG8Uint;
    case Format::RG8Sint:
        return WGPUTextureFormat_RG8Sint;
    case Format::RG8Unorm:
        return WGPUTextureFormat_RG8Unorm;
    case Format::RG8Snorm:
        return WGPUTextureFormat_RG8Snorm;

    case Format::RGBA8Uint:
        return WGPUTextureFormat_RGBA8Uint;
    case Format::RGBA8Sint:
        return WGPUTextureFormat_RGBA8Sint;
    case Format::RGBA8Unorm:
        return WGPUTextureFormat_RGBA8Unorm;
    case Format::RGBA8UnormSrgb:
        return WGPUTextureFormat_RGBA8UnormSrgb;
    case Format::RGBA8Snorm:
        return WGPUTextureFormat_RGBA8Snorm;

    case Format::BGRA8Unorm:
        return WGPUTextureFormat_BGRA8Unorm;
    case Format::BGRA8UnormSrgb:
        return WGPUTextureFormat_BGRA8UnormSrgb;
    case Format::BGRX8Unorm:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::BGRX8UnormSrgb:
        return WGPUTextureFormat_Undefined; // not supported

    case Format::R16Uint:
        return WGPUTextureFormat_R16Uint;
    case Format::R16Sint:
        return WGPUTextureFormat_R16Sint;
    case Format::R16Unorm:
        return WGPUTextureFormat_R16Unorm;
    case Format::R16Snorm:
        return WGPUTextureFormat_R16Snorm;
    case Format::R16Float:
        return WGPUTextureFormat_R16Float;

    case Format::RG16Uint:
        return WGPUTextureFormat_RG16Uint;
    case Format::RG16Sint:
        return WGPUTextureFormat_RG16Sint;
    case Format::RG16Unorm:
        return WGPUTextureFormat_RG16Unorm;
    case Format::RG16Snorm:
        return WGPUTextureFormat_RG16Snorm;
    case Format::RG16Float:
        return WGPUTextureFormat_RG16Float;

    case Format::RGBA16Uint:
        return WGPUTextureFormat_RGBA16Uint;
    case Format::RGBA16Sint:
        return WGPUTextureFormat_RGBA16Sint;
    case Format::RGBA16Unorm:
        return WGPUTextureFormat_RGBA16Unorm;
    case Format::RGBA16Snorm:
        return WGPUTextureFormat_RGBA16Snorm;
    case Format::RGBA16Float:
        return WGPUTextureFormat_RGBA16Float;

    case Format::R32Uint:
        return WGPUTextureFormat_R32Uint;
    case Format::R32Sint:
        return WGPUTextureFormat_R32Sint;
    case Format::R32Float:
        return WGPUTextureFormat_R32Float;

    case Format::RG32Uint:
        return WGPUTextureFormat_RG32Uint;
    case Format::RG32Sint:
        return WGPUTextureFormat_RG32Sint;
    case Format::RG32Float:
        return WGPUTextureFormat_RG32Float;

    case Format::RGB32Uint:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::RGB32Sint:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::RGB32Float:
        return WGPUTextureFormat_Undefined; // not supported

    case Format::RGBA32Uint:
        return WGPUTextureFormat_RGBA32Uint;
    case Format::RGBA32Sint:
        return WGPUTextureFormat_RGBA32Sint;
    case Format::RGBA32Float:
        return WGPUTextureFormat_RGBA32Float;

    case Format::R64Uint:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::R64Sint:
        return WGPUTextureFormat_Undefined; // not supported

    case Format::BGRA4Unorm:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::B5G6R5Unorm:
        return WGPUTextureFormat_Undefined; // not supported
    case Format::BGR5A1Unorm:
        return WGPUTextureFormat_Undefined; // not supported

    case Format::RGB9E5Ufloat:
        return WGPUTextureFormat_RGB9E5Ufloat;
    case Format::RGB10A2Uint:
        return WGPUTextureFormat_RGB10A2Uint;
    case Format::RGB10A2Unorm:
        return WGPUTextureFormat_RGB10A2Unorm;
    case Format::R11G11B10Float:
        return WGPUTextureFormat_RG11B10Ufloat;

    case Format::D32Float:
        return WGPUTextureFormat_Depth32Float;
    case Format::D16Unorm:
        return WGPUTextureFormat_Depth16Unorm;
    case Format::D32FloatS8Uint:
        return WGPUTextureFormat_Depth32FloatStencil8;

    case Format::BC1Unorm:
        return WGPUTextureFormat_BC1RGBAUnorm;
    case Format::BC1UnormSrgb:
        return WGPUTextureFormat_BC1RGBAUnormSrgb;
    case Format::BC2Unorm:
        return WGPUTextureFormat_BC2RGBAUnorm;
    case Format::BC2UnormSrgb:
        return WGPUTextureFormat_BC2RGBAUnormSrgb;
    case Format::BC3Unorm:
        return WGPUTextureFormat_BC3RGBAUnorm;
    case Format::BC3UnormSrgb:
        return WGPUTextureFormat_BC3RGBAUnormSrgb;
    case Format::BC4Unorm:
        return WGPUTextureFormat_BC4RUnorm;
    case Format::BC4Snorm:
        return WGPUTextureFormat_BC4RSnorm;
    case Format::BC5Unorm:
        return WGPUTextureFormat_BC5RGUnorm;
    case Format::BC5Snorm:
        return WGPUTextureFormat_BC5RGSnorm;
    case Format::BC6HUfloat:
        return WGPUTextureFormat_BC6HRGBUfloat;
    case Format::BC6HSfloat:
        return WGPUTextureFormat_BC6HRGBFloat;
    case Format::BC7Unorm:
        return WGPUTextureFormat_BC7RGBAUnorm;
    case Format::BC7UnormSrgb:
        return WGPUTextureFormat_BC7RGBAUnormSrgb;

    default:
        return WGPUTextureFormat_Undefined;
    }
}

WGPUVertexFormat translateVertexFormat(Format format)
{
    switch (format)
    {
    case Format::RG8Uint:
        return WGPUVertexFormat_Uint8x2;
    case Format::RG8Sint:
        return WGPUVertexFormat_Sint8x2;
    case Format::RG8Unorm:
        return WGPUVertexFormat_Unorm8x2;
    case Format::RG8Snorm:
        return WGPUVertexFormat_Snorm8x2;

    case Format::RGBA8Uint:
        return WGPUVertexFormat_Uint8x4;
    case Format::RGBA8Sint:
        return WGPUVertexFormat_Sint8x4;
    case Format::RGBA8Unorm:
        return WGPUVertexFormat_Unorm8x4;
    case Format::RGBA8Snorm:
        return WGPUVertexFormat_Snorm8x4;

    case Format::RG16Uint:
        return WGPUVertexFormat_Uint16x2;
    case Format::RG16Sint:
        return WGPUVertexFormat_Sint16x2;
    case Format::RG16Unorm:
        return WGPUVertexFormat_Unorm16x2;
    case Format::RG16Snorm:
        return WGPUVertexFormat_Snorm16x2;
    case Format::RG16Float:
        return WGPUVertexFormat_Float16x2;

    case Format::RGBA16Uint:
        return WGPUVertexFormat_Uint16x4;
    case Format::RGBA16Sint:
        return WGPUVertexFormat_Sint16x4;
    case Format::RGBA16Unorm:
        return WGPUVertexFormat_Unorm16x4;
    case Format::RGBA16Snorm:
        return WGPUVertexFormat_Snorm16x4;
    case Format::RGBA16Float:
        return WGPUVertexFormat_Float16x4;

    case Format::R32Uint:
        return WGPUVertexFormat_Uint32;
    case Format::R32Sint:
        return WGPUVertexFormat_Sint32;
    case Format::R32Float:
        return WGPUVertexFormat_Float32;

    case Format::RG32Uint:
        return WGPUVertexFormat_Uint32x2;
    case Format::RG32Sint:
        return WGPUVertexFormat_Sint32x2;
    case Format::RG32Float:
        return WGPUVertexFormat_Float32x2;

    case Format::RGB32Uint:
        return WGPUVertexFormat_Uint32x3;
    case Format::RGB32Sint:
        return WGPUVertexFormat_Sint32x3;
    case Format::RGB32Float:
        return WGPUVertexFormat_Float32x3;

    case Format::RGBA32Uint:
        return WGPUVertexFormat_Uint32x4;
    case Format::RGBA32Sint:
        return WGPUVertexFormat_Sint32x4;
    case Format::RGBA32Float:
        return WGPUVertexFormat_Float32x4;

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
    if (is_set(usage, TextureUsage::DepthStencil))
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

WGPUTextureViewDimension translateTextureViewDimension(TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1D:
        return WGPUTextureViewDimension_1D;
    case TextureType::Texture1DArray:
        return WGPUTextureViewDimension_Undefined;
    case TextureType::Texture2D:
    case TextureType::Texture2DMS:
        return WGPUTextureViewDimension_2D;
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMSArray:
        return WGPUTextureViewDimension_2DArray;
    case TextureType::TextureCube:
        return WGPUTextureViewDimension_Cube;
    case TextureType::TextureCubeArray:
        return WGPUTextureViewDimension_CubeArray;
    case TextureType::Texture3D:
        return WGPUTextureViewDimension_3D;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureType value");
    return WGPUTextureViewDimension_Undefined;
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
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureAspect value");
    return WGPUTextureAspect_Undefined;
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
        return WGPUAddressMode_Undefined; // not supported (warn in validation)
    case TextureAddressingMode::MirrorRepeat:
        return WGPUAddressMode_MirrorRepeat;
    case TextureAddressingMode::MirrorOnce:
        return WGPUAddressMode_Undefined; // not supported (warn in validation)
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureAddressingMode value");
    return WGPUAddressMode_Repeat;
}

WGPUFilterMode translateFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return WGPUFilterMode_Nearest;
    case TextureFilteringMode::Linear:
        return WGPUFilterMode_Linear;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureFilteringMode value");
    return WGPUFilterMode_Undefined;
}

WGPUMipmapFilterMode translateMipmapFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return WGPUMipmapFilterMode_Nearest;
    case TextureFilteringMode::Linear:
        return WGPUMipmapFilterMode_Linear;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid TextureFilteringMode value");
    return WGPUMipmapFilterMode_Undefined;
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
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid ComparisonFunc value");
    return WGPUCompareFunction_Undefined;
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
        return WGPUPrimitiveTopology_Undefined; // not supported (warn in validation)
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid PrimitiveTopology value");
    return WGPUPrimitiveTopology_Undefined;
}

WGPUFrontFace translateFrontFace(FrontFaceMode mode)
{
    switch (mode)
    {
    case FrontFaceMode::CounterClockwise:
        return WGPUFrontFace_CCW;
    case FrontFaceMode::Clockwise:
        return WGPUFrontFace_CW;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid FrontFaceMode value");
    return WGPUFrontFace_Undefined;
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
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid CullMode value");
    return WGPUCullMode_Undefined;
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
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid StencilOp value");
    return WGPUStencilOperation_Undefined;
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
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid BlendFactor value");
    return WGPUBlendFactor_Undefined;
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
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid BlendOp value");
    return WGPUBlendOperation_Undefined;
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
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid LoadOp value");
    return WGPULoadOp_Undefined;
}

WGPUStoreOp translateStoreOp(StoreOp op)
{
    switch (op)
    {
    case StoreOp::Store:
        return WGPUStoreOp_Store;
    case StoreOp::DontCare:
        return WGPUStoreOp_Undefined;
    }
    SLANG_RHI_ASSERT_FAILURE("Invalid StoreOp value");
    return WGPUStoreOp_Undefined;
}

} // namespace rhi::wgpu

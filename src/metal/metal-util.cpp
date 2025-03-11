#include "metal-util.h"
#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>

namespace rhi::metal {

const FormatMapping& MetalUtil::getFormatMapping(Format format)
{
    FormatMapping mappings[] = {
        // clang-format off
        // format                       pixelFormat                             vertexFormat                            attributeFormat
        { Format::Undefined,            MTL::PixelFormatInvalid,                MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },

        { Format::R8_UINT,              MTL::PixelFormatR8Uint,                 MTL::VertexFormatUChar,                 MTL::AttributeFormatUChar                   },
        { Format::R8_SINT,              MTL::PixelFormatR8Sint,                 MTL::VertexFormatChar,                  MTL::AttributeFormatChar                    },
        { Format::R8_UNORM,             MTL::PixelFormatR8Unorm,                MTL::VertexFormatUCharNormalized,       MTL::AttributeFormatUCharNormalized         },
        { Format::R8_SNORM,             MTL::PixelFormatR8Snorm,                MTL::VertexFormatCharNormalized,        MTL::AttributeFormatCharNormalized          },

        { Format::R8G8_UINT,            MTL::PixelFormatRG8Uint,                MTL::VertexFormatUChar2,                MTL::AttributeFormatUChar2                  },
        { Format::R8G8_SINT,            MTL::PixelFormatRG8Sint,                MTL::VertexFormatChar2,                 MTL::AttributeFormatChar2                   },
        { Format::R8G8_UNORM,           MTL::PixelFormatRG8Unorm,               MTL::VertexFormatUChar2Normalized,      MTL::AttributeFormatUChar2Normalized        },
        { Format::R8G8_SNORM,           MTL::PixelFormatRG8Snorm,               MTL::VertexFormatChar2Normalized,       MTL::AttributeFormatChar2Normalized         },

        { Format::R8G8B8A8_UINT,        MTL::PixelFormatRGBA8Uint,              MTL::VertexFormatUChar4,                MTL::AttributeFormatUChar4                  },
        { Format::R8G8B8A8_SINT,        MTL::PixelFormatRGBA8Sint,              MTL::VertexFormatChar4,                 MTL::AttributeFormatChar4                   },
        { Format::R8G8B8A8_UNORM,       MTL::PixelFormatRGBA8Unorm,             MTL::VertexFormatUChar4Normalized,      MTL::AttributeFormatUChar4Normalized        },
        { Format::R8G8B8A8_UNORM_SRGB,  MTL::PixelFormatRGBA8Unorm_sRGB,        MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::R8G8B8A8_SNORM,       MTL::PixelFormatRGBA8Snorm,             MTL::VertexFormatChar4Normalized,       MTL::AttributeFormatChar4Normalized         },

        { Format::B8G8R8A8_UNORM,       MTL::PixelFormatBGRA8Unorm,             MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::B8G8R8A8_UNORM_SRGB,  MTL::PixelFormatBGRA8Unorm_sRGB,        MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::B8G8R8X8_UNORM,       MTL::PixelFormatInvalid,                MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::B8G8R8X8_UNORM_SRGB,  MTL::PixelFormatInvalid,                MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },

        { Format::R16_UINT,             MTL::PixelFormatR16Uint,                MTL::VertexFormatUShort,                MTL::AttributeFormatUShort                  },
        { Format::R16_SINT,             MTL::PixelFormatR16Sint,                MTL::VertexFormatShort,                 MTL::AttributeFormatShort                   },
        { Format::R16_UNORM,            MTL::PixelFormatR16Unorm,               MTL::VertexFormatUShortNormalized,      MTL::AttributeFormatUShortNormalized        },
        { Format::R16_SNORM,            MTL::PixelFormatR16Snorm,               MTL::VertexFormatShortNormalized,       MTL::AttributeFormatShortNormalized         },
        { Format::R16_FLOAT,            MTL::PixelFormatR16Float,               MTL::VertexFormatHalf,                  MTL::AttributeFormatHalf                    },

        { Format::R16G16_UINT,          MTL::PixelFormatRG16Uint,               MTL::VertexFormatUShort2,               MTL::AttributeFormatUShort2                 },
        { Format::R16G16_SINT,          MTL::PixelFormatRG16Sint,               MTL::VertexFormatShort2,                MTL::AttributeFormatShort2                  },
        { Format::R16G16_UNORM,         MTL::PixelFormatRG16Unorm,              MTL::VertexFormatUShort2Normalized,     MTL::AttributeFormatUShort2Normalized       },
        { Format::R16G16_SNORM,         MTL::PixelFormatRG16Snorm,              MTL::VertexFormatShort2Normalized,      MTL::AttributeFormatShort2Normalized        },
        { Format::R16G16_FLOAT,         MTL::PixelFormatRG16Float,              MTL::VertexFormatHalf2,                 MTL::AttributeFormatHalf2                   },

        { Format::R16G16B16A16_UINT,    MTL::PixelFormatRGBA16Uint,             MTL::VertexFormatUShort4,               MTL::AttributeFormatUShort4                 },
        { Format::R16G16B16A16_SINT,    MTL::PixelFormatRGBA16Sint,             MTL::VertexFormatShort4,                MTL::AttributeFormatShort4                  },
        { Format::R16G16B16A16_UNORM,   MTL::PixelFormatRGBA16Unorm,            MTL::VertexFormatUShort4Normalized,     MTL::AttributeFormatUShort4Normalized       },
        { Format::R16G16B16A16_SNORM,   MTL::PixelFormatRGBA16Snorm,            MTL::VertexFormatShort4Normalized,      MTL::AttributeFormatShort4Normalized        },
        { Format::R16G16B16A16_FLOAT,   MTL::PixelFormatRGBA16Float,            MTL::VertexFormatHalf4,                 MTL::AttributeFormatHalf4                   },

        { Format::R32_UINT,             MTL::PixelFormatR32Uint,                MTL::VertexFormatUInt,                  MTL::AttributeFormatUInt                    },
        { Format::R32_SINT,             MTL::PixelFormatR32Sint,                MTL::VertexFormatInt,                   MTL::AttributeFormatInt                     },
        { Format::R32_FLOAT,            MTL::PixelFormatR32Float,               MTL::VertexFormatFloat,                 MTL::AttributeFormatFloat                   },

        { Format::R32G32_UINT,          MTL::PixelFormatRG32Uint,               MTL::VertexFormatUInt2,                 MTL::AttributeFormatUInt2                   },
        { Format::R32G32_SINT,          MTL::PixelFormatRG32Sint,               MTL::VertexFormatInt2,                  MTL::AttributeFormatInt2                    },
        { Format::R32G32_FLOAT,         MTL::PixelFormatRG32Float,              MTL::VertexFormatFloat2,                MTL::AttributeFormatFloat2                  },

        { Format::R32G32B32_UINT,       MTL::PixelFormatInvalid,                MTL::VertexFormatUInt3,                 MTL::AttributeFormatUInt3                   },
        { Format::R32G32B32_SINT,       MTL::PixelFormatInvalid,                MTL::VertexFormatInt3,                  MTL::AttributeFormatInt3                    },
        { Format::R32G32B32_FLOAT,      MTL::PixelFormatInvalid,                MTL::VertexFormatFloat3,                MTL::AttributeFormatFloat3                  },

        { Format::R32G32B32A32_UINT,    MTL::PixelFormatRGBA32Uint,             MTL::VertexFormatUInt4,                 MTL::AttributeFormatUInt4                   },
        { Format::R32G32B32A32_SINT,    MTL::PixelFormatRGBA32Sint,             MTL::VertexFormatInt4,                  MTL::AttributeFormatInt4                    },
        { Format::R32G32B32A32_FLOAT,   MTL::PixelFormatRGBA32Float,            MTL::VertexFormatFloat4,                MTL::AttributeFormatFloat4                  },

        { Format::R64_UINT,             MTL::PixelFormatInvalid,                MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::R64_SINT,             MTL::PixelFormatInvalid,                MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },

        { Format::B4G4R4A4_UNORM,       MTL::PixelFormatInvalid,                MTL::VertexFormatUChar4Normalized_BGRA, MTL::AttributeFormatUChar4Normalized_BGRA   },
        { Format::B5G6R5_UNORM,         MTL::PixelFormatB5G6R5Unorm,            MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::B5G5R5A1_UNORM,       MTL::PixelFormatBGR5A1Unorm,            MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },

        { Format::R9G9B9E5_SHAREDEXP,   MTL::PixelFormatRGB9E5Float,            MTL::VertexFormatFloatRGB9E5,           MTL::AttributeFormatFloatRGB9E5             },
        { Format::R10G10B10A2_UINT,     MTL::PixelFormatRGB10A2Uint,            MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::R10G10B10A2_UNORM,    MTL::PixelFormatRGB10A2Unorm,           MTL::VertexFormatUInt1010102Normalized, MTL::AttributeFormatUInt1010102Normalized   },
        { Format::R11G11B10_FLOAT,      MTL::PixelFormatRG11B10Float,           MTL::VertexFormatFloatRG11B10,          MTL::AttributeFormatFloatRG11B10            },

        { Format::D32_FLOAT,            MTL::PixelFormatDepth32Float,           MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::D16_UNORM,            MTL::PixelFormatDepth16Unorm,           MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::D32_FLOAT_S8_UINT,    MTL::PixelFormatDepth32Float_Stencil8,  MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },

        { Format::BC1_UNORM,            MTL::PixelFormatBC1_RGBA,               MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC1_UNORM_SRGB,       MTL::PixelFormatBC1_RGBA_sRGB,          MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC2_UNORM,            MTL::PixelFormatBC2_RGBA,               MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC2_UNORM_SRGB,       MTL::PixelFormatBC2_RGBA_sRGB,          MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC3_UNORM,            MTL::PixelFormatBC3_RGBA,               MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC3_UNORM_SRGB,       MTL::PixelFormatBC3_RGBA_sRGB,          MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC4_UNORM,            MTL::PixelFormatBC4_RUnorm,             MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC4_SNORM,            MTL::PixelFormatBC4_RSnorm,             MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC5_UNORM,            MTL::PixelFormatBC5_RGUnorm,            MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC5_SNORM,            MTL::PixelFormatBC5_RGSnorm,            MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC6H_UF16,            MTL::PixelFormatBC6H_RGBUfloat,         MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC6H_SF16,            MTL::PixelFormatBC6H_RGBFloat,          MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC7_UNORM,            MTL::PixelFormatBC7_RGBAUnorm,          MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        { Format::BC7_UNORM_SRGB,       MTL::PixelFormatBC7_RGBAUnorm_sRGB,     MTL::VertexFormatInvalid,               MTL::AttributeFormatInvalid                 },
        // clang-format on
    };

    static_assert(SLANG_COUNT_OF(mappings) == size_t(Format::_Count), "Missing format mapping");
    SLANG_RHI_ASSERT(uint32_t(format) < uint32_t(Format::_Count));
    return mappings[int(format)];
}


MTL::PixelFormat MetalUtil::translatePixelFormat(Format format)
{
    return getFormatMapping(format).pixelFormat;
}

MTL::VertexFormat MetalUtil::translateVertexFormat(Format format)
{
    return getFormatMapping(format).vertexFormat;
    // Unsupported vertex formats:
    // - VertexFormatUChar3
    // - VertexFormatChar3
    // - VertexFormatUChar3Normalized
    // - VertexFormatChar3Normalized
    // - VertexFormatUShort3
    // - VertexFormatShort3
    // - VertexFormatUShort3Normalized
    // - VertexFormatShort3Normalized
    // - VertexFormatHalf3
    // - VertexFormatInt1010102Normalized
}

MTL::AttributeFormat MetalUtil::translateAttributeFormat(Format format)
{
    return getFormatMapping(format).attributeFormat;
    // Unsupported attribute formats:
    // - AttributeFormatUChar3
    // - AttributeFormatChar3
    // - AttributeFormatUChar3Normalized
    // - AttributeFormatChar3Normalized
    // - AttributeFormatUShort3
    // - AttributeFormatShort3
    // - AttributeFormatUShort3Normalized
    // - AttributeFormatShort3Normalized
    // - AttributeFormatHalf3
    // - AttributeFormatInt1010102Normalized
}

bool MetalUtil::isDepthFormat(MTL::PixelFormat format)
{
    switch (format)
    {
    case MTL::PixelFormatDepth16Unorm:
    case MTL::PixelFormatDepth32Float:
    case MTL::PixelFormatDepth24Unorm_Stencil8:
    case MTL::PixelFormatDepth32Float_Stencil8:
        return true;
    default:
        return false;
    }
}

bool MetalUtil::isStencilFormat(MTL::PixelFormat format)
{
    switch (format)
    {
    case MTL::PixelFormatStencil8:
    case MTL::PixelFormatDepth24Unorm_Stencil8:
    case MTL::PixelFormatDepth32Float_Stencil8:
    case MTL::PixelFormatX32_Stencil8:
    case MTL::PixelFormatX24_Stencil8:
        return true;
    default:
        return false;
    }
}

MTL::TextureType MetalUtil::translateTextureType(TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1D:
        return MTL::TextureType1D;
    case TextureType::Texture1DArray:
        return MTL::TextureType1DArray;
    case TextureType::Texture2D:
        return MTL::TextureType2D;
    case TextureType::Texture2DArray:
        return MTL::TextureType2DArray;
    case TextureType::Texture2DMS:
        return MTL::TextureType2DMultisample;
    case TextureType::Texture2DMSArray:
        return MTL::TextureType2DMultisampleArray;
    case TextureType::Texture3D:
        return MTL::TextureType3D;
    case TextureType::TextureCube:
        return MTL::TextureTypeCube;
    case TextureType::TextureCubeArray:
        return MTL::TextureTypeCubeArray;
    default:
        return MTL::TextureType(0);
    }
}

MTL::SamplerMinMagFilter MetalUtil::translateSamplerMinMagFilter(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return MTL::SamplerMinMagFilterNearest;
    case TextureFilteringMode::Linear:
        return MTL::SamplerMinMagFilterLinear;
    default:
        return MTL::SamplerMinMagFilter(0);
    }
}

MTL::SamplerMipFilter MetalUtil::translateSamplerMipFilter(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return MTL::SamplerMipFilterNearest;
    case TextureFilteringMode::Linear:
        return MTL::SamplerMipFilterLinear;
    default:
        return MTL::SamplerMipFilter(0);
    }
}

MTL::SamplerAddressMode MetalUtil::translateSamplerAddressMode(TextureAddressingMode mode)
{
    switch (mode)
    {
    case TextureAddressingMode::Wrap:
        return MTL::SamplerAddressModeRepeat;
    case TextureAddressingMode::ClampToEdge:
        return MTL::SamplerAddressModeClampToEdge;
    case TextureAddressingMode::ClampToBorder:
        return MTL::SamplerAddressModeClampToBorderColor;
    case TextureAddressingMode::MirrorRepeat:
        return MTL::SamplerAddressModeMirrorRepeat;
    case TextureAddressingMode::MirrorOnce:
        return MTL::SamplerAddressModeMirrorClampToEdge;
    default:
        return MTL::SamplerAddressMode(0);
    }
}

MTL::CompareFunction MetalUtil::translateCompareFunction(ComparisonFunc func)
{
    switch (func)
    {
    case ComparisonFunc::Never:
        return MTL::CompareFunctionNever;
    case ComparisonFunc::Less:
        return MTL::CompareFunctionLess;
    case ComparisonFunc::Equal:
        return MTL::CompareFunctionEqual;
    case ComparisonFunc::LessEqual:
        return MTL::CompareFunctionLessEqual;
    case ComparisonFunc::Greater:
        return MTL::CompareFunctionGreater;
    case ComparisonFunc::NotEqual:
        return MTL::CompareFunctionNotEqual;
    case ComparisonFunc::GreaterEqual:
        return MTL::CompareFunctionGreaterEqual;
    case ComparisonFunc::Always:
        return MTL::CompareFunctionAlways;
    default:
        return MTL::CompareFunction(0);
    }
}

MTL::StencilOperation MetalUtil::translateStencilOperation(StencilOp op)
{
    switch (op)
    {
    case StencilOp::Keep:
        return MTL::StencilOperationKeep;
    case StencilOp::Zero:
        return MTL::StencilOperationZero;
    case StencilOp::Replace:
        return MTL::StencilOperationReplace;
    case StencilOp::IncrementSaturate:
        return MTL::StencilOperationIncrementClamp;
    case StencilOp::DecrementSaturate:
        return MTL::StencilOperationDecrementClamp;
    case StencilOp::Invert:
        return MTL::StencilOperationInvert;
    case StencilOp::IncrementWrap:
        return MTL::StencilOperationIncrementWrap;
    case StencilOp::DecrementWrap:
        return MTL::StencilOperationDecrementWrap;
    default:
        return MTL::StencilOperation(0);
    }
}

MTL::VertexStepFunction MetalUtil::translateVertexStepFunction(InputSlotClass slotClass)
{
    switch (slotClass)
    {
    case InputSlotClass::PerVertex:
        return MTL::VertexStepFunctionPerVertex;
    case InputSlotClass::PerInstance:
        return MTL::VertexStepFunctionPerInstance;
    default:
        return MTL::VertexStepFunctionPerVertex;
    }
}

MTL::PrimitiveType MetalUtil::translatePrimitiveType(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::PointList:
        return MTL::PrimitiveTypePoint;
    case PrimitiveTopology::LineList:
        return MTL::PrimitiveTypeLine;
    case PrimitiveTopology::LineStrip:
        return MTL::PrimitiveTypeLineStrip;
    case PrimitiveTopology::TriangleList:
        return MTL::PrimitiveTypeTriangle;
    case PrimitiveTopology::TriangleStrip:
        return MTL::PrimitiveTypeTriangleStrip;
    case PrimitiveTopology::PatchList:
    default:
        return MTL::PrimitiveType(0);
    }
}

MTL::PrimitiveTopologyClass MetalUtil::translatePrimitiveTopologyClass(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::PointList:
        return MTL::PrimitiveTopologyClassPoint;
    case PrimitiveTopology::LineList:
    case PrimitiveTopology::LineStrip:
        return MTL::PrimitiveTopologyClassLine;
    case PrimitiveTopology::TriangleList:
    case PrimitiveTopology::TriangleStrip:
        return MTL::PrimitiveTopologyClassTriangle;
    case PrimitiveTopology::PatchList:
    default:
        return MTL::PrimitiveTopologyClass(0);
    }
}

MTL::BlendFactor MetalUtil::translateBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
    case BlendFactor::Zero:
        return MTL::BlendFactorZero;
    case BlendFactor::One:
        return MTL::BlendFactorOne;
    case BlendFactor::SrcColor:
        return MTL::BlendFactorSourceColor;
    case BlendFactor::InvSrcColor:
        return MTL::BlendFactorOneMinusSourceColor;
    case BlendFactor::SrcAlpha:
        return MTL::BlendFactorSourceAlpha;
    case BlendFactor::InvSrcAlpha:
        return MTL::BlendFactorOneMinusSourceAlpha;
    case BlendFactor::DestAlpha:
        return MTL::BlendFactorDestinationAlpha;
    case BlendFactor::InvDestAlpha:
        return MTL::BlendFactorOneMinusDestinationAlpha;
    case BlendFactor::DestColor:
        return MTL::BlendFactorDestinationColor;
    case BlendFactor::InvDestColor:
        return MTL::BlendFactorOneMinusDestinationColor;
    case BlendFactor::SrcAlphaSaturate:
        return MTL::BlendFactorSourceAlphaSaturated;
    case BlendFactor::BlendColor:
        return MTL::BlendFactorBlendColor;
    case BlendFactor::InvBlendColor:
        return MTL::BlendFactorOneMinusBlendColor;
    case BlendFactor::SecondarySrcColor:
        return MTL::BlendFactorSource1Color;
    case BlendFactor::InvSecondarySrcColor:
        return MTL::BlendFactorOneMinusSource1Color;
    case BlendFactor::SecondarySrcAlpha:
        return MTL::BlendFactorSource1Alpha;
    case BlendFactor::InvSecondarySrcAlpha:
        return MTL::BlendFactorOneMinusSource1Alpha;
    default:
        return MTL::BlendFactor(0);
    }
}

MTL::BlendOperation MetalUtil::translateBlendOperation(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add:
        return MTL::BlendOperationAdd;
    case BlendOp::Subtract:
        return MTL::BlendOperationSubtract;
    case BlendOp::ReverseSubtract:
        return MTL::BlendOperationReverseSubtract;
    case BlendOp::Min:
        return MTL::BlendOperationMin;
    case BlendOp::Max:
        return MTL::BlendOperationMax;
    default:
        return MTL::BlendOperation(0);
    }
}

MTL::ColorWriteMask MetalUtil::translateColorWriteMask(RenderTargetWriteMask::Type mask)
{
    MTL::ColorWriteMask result = MTL::ColorWriteMaskNone;
    if (mask & RenderTargetWriteMask::EnableRed)
        result |= MTL::ColorWriteMaskRed;
    if (mask & RenderTargetWriteMask::EnableGreen)
        result |= MTL::ColorWriteMaskGreen;
    if (mask & RenderTargetWriteMask::EnableBlue)
        result |= MTL::ColorWriteMaskBlue;
    if (mask & RenderTargetWriteMask::EnableAlpha)
        result |= MTL::ColorWriteMaskAlpha;
    return result;
}

MTL::Winding MetalUtil::translateWinding(FrontFaceMode mode)
{
    switch (mode)
    {
    case FrontFaceMode::CounterClockwise:
        return MTL::WindingCounterClockwise;
    case FrontFaceMode::Clockwise:
        return MTL::WindingClockwise;
    default:
        return MTL::Winding(0);
    }
}

MTL::CullMode MetalUtil::translateCullMode(CullMode mode)
{
    switch (mode)
    {
    case CullMode::None:
        return MTL::CullModeNone;
    case CullMode::Front:
        return MTL::CullModeFront;
    case CullMode::Back:
        return MTL::CullModeBack;
    default:
        return MTL::CullMode(0);
    }
}

MTL::TriangleFillMode MetalUtil::translateTriangleFillMode(FillMode mode)
{
    switch (mode)
    {
    case FillMode::Solid:
        return MTL::TriangleFillModeFill;
    case FillMode::Wireframe:
        return MTL::TriangleFillModeLines;
    default:
        return MTL::TriangleFillMode(0);
    }
}

MTL::LoadAction MetalUtil::translateLoadOp(LoadOp loadOp)
{
    switch (loadOp)
    {
    case LoadOp::Load:
        return MTL::LoadActionLoad;
    case LoadOp::Clear:
        return MTL::LoadActionClear;
    case LoadOp::DontCare:
        return MTL::LoadActionDontCare;
    default:
        return MTL::LoadAction(0);
    }
}

MTL::StoreAction MetalUtil::translateStoreOp(StoreOp storeOp, bool resolve)
{
    switch (storeOp)
    {
    case StoreOp::Store:
        return resolve ? MTL::StoreActionStoreAndMultisampleResolve : MTL::StoreActionStore;
    case StoreOp::DontCare:
        return resolve ? MTL::StoreActionMultisampleResolve : MTL::StoreActionDontCare;
    default:
        return MTL::StoreAction(0);
    }
}

} // namespace rhi::metal

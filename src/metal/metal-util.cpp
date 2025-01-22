#include "metal-util.h"
#include "metal-buffer.h"
#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>

namespace rhi::metal {

MTL::PixelFormat MetalUtil::translatePixelFormat(Format format)
{
    switch (format)
    {
    case Format::R32G32B32A32_TYPELESS:
        return MTL::PixelFormatRGBA32Float;
    case Format::R32G32B32_TYPELESS:
        return MTL::PixelFormatInvalid;
    case Format::R32G32_TYPELESS:
        return MTL::PixelFormatRG32Float;
    case Format::R32_TYPELESS:
        return MTL::PixelFormatR32Float;

    case Format::R16G16B16A16_TYPELESS:
        return MTL::PixelFormatRGBA16Float;
    case Format::R16G16_TYPELESS:
        return MTL::PixelFormatRG16Float;
    case Format::R16_TYPELESS:
        return MTL::PixelFormatR16Float;

    case Format::R8G8B8A8_TYPELESS:
        return MTL::PixelFormatRGBA8Unorm;
    case Format::R8G8_TYPELESS:
        return MTL::PixelFormatRG8Unorm;
    case Format::R8_TYPELESS:
        return MTL::PixelFormatR8Unorm;
    case Format::B8G8R8A8_TYPELESS:
        return MTL::PixelFormatBGRA8Unorm;

    case Format::R32G32B32A32_FLOAT:
        return MTL::PixelFormatRGBA32Float;
    case Format::R32G32B32_FLOAT:
        return MTL::PixelFormatInvalid;
    case Format::R32G32_FLOAT:
        return MTL::PixelFormatRG32Float;
    case Format::R32_FLOAT:
        return MTL::PixelFormatR32Float;

    case Format::R16G16B16A16_FLOAT:
        return MTL::PixelFormatRGBA16Float;
    case Format::R16G16_FLOAT:
        return MTL::PixelFormatRG16Float;
    case Format::R16_FLOAT:
        return MTL::PixelFormatR16Float;

    case Format::R32G32B32A32_UINT:
        return MTL::PixelFormatRGBA32Uint;
    case Format::R32G32B32_UINT:
        return MTL::PixelFormatInvalid;
    case Format::R32G32_UINT:
        return MTL::PixelFormatRG32Uint;
    case Format::R32_UINT:
        return MTL::PixelFormatR32Uint;

    case Format::R16G16B16A16_UINT:
        return MTL::PixelFormatRGBA16Uint;
    case Format::R16G16_UINT:
        return MTL::PixelFormatRG16Uint;
    case Format::R16_UINT:
        return MTL::PixelFormatR16Uint;

    case Format::R8G8B8A8_UINT:
        return MTL::PixelFormatRGBA8Uint;
    case Format::R8G8_UINT:
        return MTL::PixelFormatRG8Uint;
    case Format::R8_UINT:
        return MTL::PixelFormatR8Uint;

    case Format::R32G32B32A32_SINT:
        return MTL::PixelFormatRGBA32Sint;
    case Format::R32G32B32_SINT:
        return MTL::PixelFormatInvalid;
    case Format::R32G32_SINT:
        return MTL::PixelFormatRG32Sint;
    case Format::R32_SINT:
        return MTL::PixelFormatR32Sint;

    case Format::R16G16B16A16_SINT:
        return MTL::PixelFormatRGBA16Sint;
    case Format::R16G16_SINT:
        return MTL::PixelFormatRG16Sint;
    case Format::R16_SINT:
        return MTL::PixelFormatR16Sint;

    case Format::R8G8B8A8_SINT:
        return MTL::PixelFormatRGBA8Sint;
    case Format::R8G8_SINT:
        return MTL::PixelFormatRG8Sint;
    case Format::R8_SINT:
        return MTL::PixelFormatR8Sint;

    case Format::R16G16B16A16_UNORM:
        return MTL::PixelFormatRGBA16Unorm;
    case Format::R16G16_UNORM:
        return MTL::PixelFormatRG16Unorm;
    case Format::R16_UNORM:
        return MTL::PixelFormatR16Unorm;

    case Format::R8G8B8A8_UNORM:
        return MTL::PixelFormatRGBA8Unorm;
    case Format::R8G8B8A8_UNORM_SRGB:
        return MTL::PixelFormatRGBA8Unorm_sRGB;
    case Format::R8G8_UNORM:
        return MTL::PixelFormatRG8Unorm;
    case Format::R8_UNORM:
        return MTL::PixelFormatR8Unorm;
    case Format::B8G8R8A8_UNORM:
        return MTL::PixelFormatBGRA8Unorm;
    case Format::B8G8R8A8_UNORM_SRGB:
        return MTL::PixelFormatBGRA8Unorm_sRGB;
    case Format::B8G8R8X8_UNORM:
        return MTL::PixelFormatInvalid;
    case Format::B8G8R8X8_UNORM_SRGB:
        return MTL::PixelFormatInvalid;

    case Format::R16G16B16A16_SNORM:
        return MTL::PixelFormatRGBA16Snorm;
    case Format::R16G16_SNORM:
        return MTL::PixelFormatRG16Snorm;
    case Format::R16_SNORM:
        return MTL::PixelFormatR16Snorm;

    case Format::R8G8B8A8_SNORM:
        return MTL::PixelFormatRGBA8Snorm;
    case Format::R8G8_SNORM:
        return MTL::PixelFormatRG8Snorm;
    case Format::R8_SNORM:
        return MTL::PixelFormatR8Snorm;

    case Format::D32_FLOAT:
        return MTL::PixelFormatDepth32Float;
    case Format::D16_UNORM:
        return MTL::PixelFormatDepth16Unorm;
    case Format::D32_FLOAT_S8_UINT:
        return MTL::PixelFormatDepth32Float_Stencil8;
    case Format::R32_FLOAT_X32_TYPELESS:
        return MTL::PixelFormatInvalid;

    case Format::B4G4R4A4_UNORM:
        return MTL::PixelFormatABGR4Unorm;
    case Format::B5G6R5_UNORM:
        return MTL::PixelFormatB5G6R5Unorm;
    case Format::B5G5R5A1_UNORM:
        return MTL::PixelFormatA1BGR5Unorm;

    case Format::R9G9B9E5_SHAREDEXP:
        return MTL::PixelFormatRGB9E5Float;
    case Format::R10G10B10A2_TYPELESS:
        return MTL::PixelFormatInvalid;
    case Format::R10G10B10A2_UINT:
        return MTL::PixelFormatRGB10A2Uint;
    case Format::R10G10B10A2_UNORM:
        return MTL::PixelFormatRGB10A2Unorm;
    case Format::R11G11B10_FLOAT:
        return MTL::PixelFormatRG11B10Float;

    case Format::BC1_UNORM:
        return MTL::PixelFormatBC1_RGBA;
    case Format::BC1_UNORM_SRGB:
        return MTL::PixelFormatBC1_RGBA_sRGB;
    case Format::BC2_UNORM:
        return MTL::PixelFormatBC2_RGBA;
    case Format::BC2_UNORM_SRGB:
        return MTL::PixelFormatBC2_RGBA_sRGB;
    case Format::BC3_UNORM:
        return MTL::PixelFormatBC3_RGBA;
    case Format::BC3_UNORM_SRGB:
        return MTL::PixelFormatBC3_RGBA_sRGB;
    case Format::BC4_UNORM:
        return MTL::PixelFormatBC4_RUnorm;
    case Format::BC4_SNORM:
        return MTL::PixelFormatBC4_RSnorm;
    case Format::BC5_UNORM:
        return MTL::PixelFormatBC5_RGUnorm;
    case Format::BC5_SNORM:
        return MTL::PixelFormatBC5_RGSnorm;
    case Format::BC6H_UF16:
        return MTL::PixelFormatBC6H_RGBUfloat;
    case Format::BC6H_SF16:
        return MTL::PixelFormatBC6H_RGBFloat;
    case Format::BC7_UNORM:
        return MTL::PixelFormatBC7_RGBAUnorm;
    case Format::BC7_UNORM_SRGB:
        return MTL::PixelFormatBC7_RGBAUnorm_sRGB;

    default:
        return MTL::PixelFormatInvalid;
    }
}

MTL::VertexFormat MetalUtil::translateVertexFormat(Format format)
{
    switch (format)
    {
    case Format::R8G8_UINT:
        return MTL::VertexFormatUChar2;
    // VertexFormatUChar3
    case Format::R8G8B8A8_UINT:
        return MTL::VertexFormatUChar4;
    case Format::R8G8_SINT:
        return MTL::VertexFormatChar2;
    // return VertexFormatChar3
    case Format::R8G8B8A8_SINT:
        return MTL::VertexFormatChar4;
    case Format::R8G8_UNORM:
        return MTL::VertexFormatUChar2Normalized;
    // return VertexFormatUChar3Normalized;
    case Format::R8G8B8A8_UNORM:
        return MTL::VertexFormatUChar4Normalized;
    case Format::R8G8_SNORM:
        return MTL::VertexFormatChar2Normalized;
    // return VertexFormatChar3Normalized
    case Format::R8G8B8A8_SNORM:
        return MTL::VertexFormatChar4Normalized;
    case Format::R16G16_UINT:
        return MTL::VertexFormatUShort2;
    // return VertexFormatUShort3;
    case Format::R16G16B16A16_UINT:
        return MTL::VertexFormatUShort4;
    case Format::R16G16_SINT:
        return MTL::VertexFormatShort2;
    // return VertexFormatShort3;
    case Format::R16G16B16A16_SINT:
        return MTL::VertexFormatShort4;
    case Format::R16G16_UNORM:
        return MTL::VertexFormatUShort2Normalized;
    // return VertexFormatUShort3Normalized;
    case Format::R16G16B16A16_UNORM:
        return MTL::VertexFormatUShort4Normalized;
    case Format::R16G16_SNORM:
        return MTL::VertexFormatShort2Normalized;
    // return VertexFormatShort3Normalized;
    case Format::R16G16B16A16_SNORM:
        return MTL::VertexFormatShort4Normalized;
    case Format::R16G16_FLOAT:
        return MTL::VertexFormatHalf2;
    // return VertexFormatHalf3;
    case Format::R16G16B16A16_FLOAT:
        return MTL::VertexFormatHalf4;
    case Format::R32_FLOAT:
        return MTL::VertexFormatFloat;
    case Format::R32G32_FLOAT:
        return MTL::VertexFormatFloat2;
    case Format::R32G32B32_FLOAT:
        return MTL::VertexFormatFloat3;
    case Format::R32G32B32A32_FLOAT:
        return MTL::VertexFormatFloat4;
    case Format::R32_SINT:
        return MTL::VertexFormatInt;
    case Format::R32G32_SINT:
        return MTL::VertexFormatInt2;
    case Format::R32G32B32_SINT:
        return MTL::VertexFormatInt3;
    case Format::R32G32B32A32_SINT:
        return MTL::VertexFormatInt4;
    case Format::R32_UINT:
        return MTL::VertexFormatUInt;
    case Format::R32G32_UINT:
        return MTL::VertexFormatUInt2;
    case Format::R32G32B32_UINT:
        return MTL::VertexFormatUInt3;
    case Format::R32G32B32A32_UINT:
        return MTL::VertexFormatUInt4;
    // return VertexFormatInt1010102Normalized;
    case Format::R10G10B10A2_UNORM:
        return MTL::VertexFormatUInt1010102Normalized;
    case Format::B4G4R4A4_UNORM:
        return MTL::VertexFormatUChar4Normalized_BGRA;
    case Format::R8_UINT:
        return MTL::VertexFormatUChar;
    case Format::R8_SINT:
        return MTL::VertexFormatChar;
    case Format::R8_UNORM:
        return MTL::VertexFormatUCharNormalized;
    case Format::R8_SNORM:
        return MTL::VertexFormatCharNormalized;
    case Format::R16_UINT:
        return MTL::VertexFormatUShort;
    case Format::R16_SINT:
        return MTL::VertexFormatShort;
    case Format::R16_UNORM:
        return MTL::VertexFormatUShortNormalized;
    case Format::R16_SNORM:
        return MTL::VertexFormatShortNormalized;
    case Format::R16_FLOAT:
        return MTL::VertexFormatHalf;
    case Format::R11G11B10_FLOAT:
        return MTL::VertexFormatFloatRG11B10;
    case Format::R9G9B9E5_SHAREDEXP:
        return MTL::VertexFormatFloatRGB9E5;
    default:
        return MTL::VertexFormatInvalid;
    }
}

MTL::AttributeFormat MetalUtil::translateAttributeFormat(Format format)
{
    switch (format)
    {
    case Format::R8G8_UINT:
        return MTL::AttributeFormatUChar2;
    // AttributeFormatUChar3
    case Format::R8G8B8A8_UINT:
        return MTL::AttributeFormatUChar4;
    case Format::R8G8_SINT:
        return MTL::AttributeFormatChar2;
    // return AttributeFormatChar3
    case Format::R8G8B8A8_SINT:
        return MTL::AttributeFormatChar4;
    case Format::R8G8_UNORM:
        return MTL::AttributeFormatUChar2Normalized;
    // return AttributeFormatUChar3Normalized;
    case Format::R8G8B8A8_UNORM:
        return MTL::AttributeFormatUChar4Normalized;
    case Format::R8G8_SNORM:
        return MTL::AttributeFormatChar2Normalized;
    // return AttributeFormatChar3Normalized
    case Format::R8G8B8A8_SNORM:
        return MTL::AttributeFormatChar4Normalized;
    case Format::R16G16_UINT:
        return MTL::AttributeFormatUShort2;
    // return AttributeFormatUShort3;
    case Format::R16G16B16A16_UINT:
        return MTL::AttributeFormatUShort4;
    case Format::R16G16_SINT:
        return MTL::AttributeFormatShort2;
    // return AttributeFormatShort3;
    case Format::R16G16B16A16_SINT:
        return MTL::AttributeFormatShort4;
    case Format::R16G16_UNORM:
        return MTL::AttributeFormatUShort2Normalized;
    // return AttributeFormatUShort3Normalized;
    case Format::R16G16B16A16_UNORM:
        return MTL::AttributeFormatUShort4Normalized;
    case Format::R16G16_SNORM:
        return MTL::AttributeFormatShort2Normalized;
    // return AttributeFormatShort3Normalized;
    case Format::R16G16B16A16_SNORM:
        return MTL::AttributeFormatShort4Normalized;
    case Format::R16G16_FLOAT:
        return MTL::AttributeFormatHalf2;
    // return AttributeFormatHalf3;
    case Format::R16G16B16A16_FLOAT:
        return MTL::AttributeFormatHalf4;
    case Format::R32_FLOAT:
        return MTL::AttributeFormatFloat;
    case Format::R32G32_FLOAT:
        return MTL::AttributeFormatFloat2;
    case Format::R32G32B32_FLOAT:
        return MTL::AttributeFormatFloat3;
    case Format::R32G32B32A32_FLOAT:
        return MTL::AttributeFormatFloat4;
    case Format::R32_SINT:
        return MTL::AttributeFormatInt;
    case Format::R32G32_SINT:
        return MTL::AttributeFormatInt2;
    case Format::R32G32B32_SINT:
        return MTL::AttributeFormatInt3;
    case Format::R32G32B32A32_SINT:
        return MTL::AttributeFormatInt4;
    case Format::R32_UINT:
        return MTL::AttributeFormatUInt;
    case Format::R32G32_UINT:
        return MTL::AttributeFormatUInt2;
    case Format::R32G32B32_UINT:
        return MTL::AttributeFormatUInt3;
    case Format::R32G32B32A32_UINT:
        return MTL::AttributeFormatUInt4;
    // return AttributeFormatInt1010102Normalized;
    case Format::R10G10B10A2_UNORM:
        return MTL::AttributeFormatUInt1010102Normalized;
    case Format::B4G4R4A4_UNORM:
        return MTL::AttributeFormatUChar4Normalized_BGRA;
    case Format::R8_UINT:
        return MTL::AttributeFormatUChar;
    case Format::R8_SINT:
        return MTL::AttributeFormatChar;
    case Format::R8_UNORM:
        return MTL::AttributeFormatUCharNormalized;
    case Format::R8_SNORM:
        return MTL::AttributeFormatCharNormalized;
    case Format::R16_UINT:
        return MTL::AttributeFormatUShort;
    case Format::R16_SINT:
        return MTL::AttributeFormatShort;
    case Format::R16_UNORM:
        return MTL::AttributeFormatUShortNormalized;
    case Format::R16_SNORM:
        return MTL::AttributeFormatShortNormalized;
    case Format::R16_FLOAT:
        return MTL::AttributeFormatHalf;
    case Format::R11G11B10_FLOAT:
        return MTL::AttributeFormatFloatRG11B10;
    case Format::R9G9B9E5_SHAREDEXP:
        return MTL::AttributeFormatFloatRGB9E5;
    default:
        return MTL::AttributeFormatInvalid;
    }
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

Result AccelerationStructureDescBuilder::build(
    const AccelerationStructureBuildDesc& buildDesc,
    const NS::Array* accelerationStructureArray,
    IDebugCallback* debugCallback
)
{
    if (buildDesc.inputCount < 1)
    {
        return SLANG_E_INVALID_ARG;
    }

    AccelerationStructureBuildInputType type = (AccelerationStructureBuildInputType&)buildDesc.inputs[0];
    for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
    {
        if ((AccelerationStructureBuildInputType&)buildDesc.inputs[i] != type)
        {
            return SLANG_E_INVALID_ARG;
        }
    }

    switch (type)
    {
    case AccelerationStructureBuildInputType::Instances:
    {
        if (buildDesc.inputCount > 1)
        {
            return SLANG_E_INVALID_ARG;
        }

        MTL::InstanceAccelerationStructureDescriptor* instanceDescriptor =
            MTL::InstanceAccelerationStructureDescriptor::alloc()->init();
        descriptor = NS::TransferPtr(instanceDescriptor);

        const AccelerationStructureBuildInputInstances& instances =
            (const AccelerationStructureBuildInputInstances&)buildDesc.inputs[0];

        instanceDescriptor->setUsage(translateBuildFlags(buildDesc.flags));
        instanceDescriptor->setInstanceDescriptorBuffer(
            checked_cast<BufferImpl*>(instances.instanceBuffer.buffer)->m_buffer.get()
        );
        instanceDescriptor->setInstanceDescriptorBufferOffset(instances.instanceBuffer.offset);
        instanceDescriptor->setInstanceDescriptorStride(instances.instanceStride);
        instanceDescriptor->setInstanceCount(instances.instanceCount);
        instanceDescriptor->setInstanceDescriptorType(MTL::AccelerationStructureInstanceDescriptorTypeUserID);
        instanceDescriptor->setInstancedAccelerationStructures(accelerationStructureArray);

        break;
    }
    case AccelerationStructureBuildInputType::Triangles:
    {
        MTL::PrimitiveAccelerationStructureDescriptor* primitiveDescriptor =
            MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init();
        descriptor = NS::TransferPtr(primitiveDescriptor);

        primitiveDescriptor->setUsage(translateBuildFlags(buildDesc.flags));

        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputTriangles& triangles =
                (const AccelerationStructureBuildInputTriangles&)buildDesc.inputs[i];
            if (triangles.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            MTL::AccelerationStructureTriangleGeometryDescriptor* triangleDescriptor =
                (MTL::AccelerationStructureTriangleGeometryDescriptor*)primitiveDescriptor->geometryDescriptors()
                    ->object(i);

            triangleDescriptor->setVertexBuffer(
                checked_cast<BufferImpl*>(triangles.vertexBuffers[0].buffer)->m_buffer.get()
            );
            triangleDescriptor->setVertexBufferOffset(triangles.vertexBuffers[0].offset);
            triangleDescriptor->setVertexFormat(MetalUtil::translateAttributeFormat(triangles.vertexFormat));
            triangleDescriptor->setVertexStride(triangles.vertexStride);

            if (triangles.indexBuffer)
            {
                triangleDescriptor->setIndexBuffer(
                    checked_cast<BufferImpl*>(triangles.indexBuffer.buffer)->m_buffer.get()
                );
                triangleDescriptor->setIndexBufferOffset(triangles.indexBuffer.offset);
                triangleDescriptor->setIndexType(
                    triangles.indexFormat == IndexFormat::UInt32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16
                );
            }

            uint32_t triangleCount = max(triangles.vertexCount, triangles.indexCount) / 3;
            triangleDescriptor->setTriangleCount(triangleCount);

            if (triangles.preTransformBuffer)
            {
                triangleDescriptor->setTransformationMatrixBuffer(
                    checked_cast<BufferImpl*>(triangles.preTransformBuffer.buffer)->m_buffer.get()
                );
                triangleDescriptor->setTransformationMatrixBufferOffset(triangles.preTransformBuffer.offset);
            }

            triangleDescriptor->setOpaque(is_set(triangles.flags, AccelerationStructureGeometryFlags::Opaque));
            triangleDescriptor->setAllowDuplicateIntersectionFunctionInvocation(
                !is_set(triangles.flags, AccelerationStructureGeometryFlags::NoDuplicateAnyHitInvocation)
            );
        }

        break;
    }
    case AccelerationStructureBuildInputType::ProceduralPrimitives:
    {
        MTL::PrimitiveAccelerationStructureDescriptor* primitiveDescriptor =
            MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init();
        descriptor = NS::TransferPtr(primitiveDescriptor);

        primitiveDescriptor->setUsage(translateBuildFlags(buildDesc.flags));

        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputProceduralPrimitives& proceduralPrimitives =
                (const AccelerationStructureBuildInputProceduralPrimitives&)buildDesc.inputs[i];
            if (proceduralPrimitives.aabbBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            MTL::AccelerationStructureBoundingBoxGeometryDescriptor* boundingDescriptor =
                (MTL::AccelerationStructureBoundingBoxGeometryDescriptor*)primitiveDescriptor->geometryDescriptors()
                    ->object(i);

            boundingDescriptor->setBoundingBoxBuffer(
                checked_cast<BufferImpl*>(proceduralPrimitives.aabbBuffers[0].buffer)->m_buffer.get()
            );
            boundingDescriptor->setBoundingBoxBufferOffset(proceduralPrimitives.aabbBuffers[0].offset);
            boundingDescriptor->setBoundingBoxStride(proceduralPrimitives.aabbStride);
            boundingDescriptor->setBoundingBoxCount(proceduralPrimitives.primitiveCount);
        }

        break;
    }
    default:
        return SLANG_E_INVALID_ARG;
    }

    return SLANG_OK;
}


} // namespace rhi::metal

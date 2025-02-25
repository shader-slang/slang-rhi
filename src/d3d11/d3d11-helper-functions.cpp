#include "d3d11-helper-functions.h"
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
    if (is_set(usage, TextureUsage::DepthRead))
        flags |= D3D11_BIND_DEPTH_STENCIL;
    if (is_set(usage, TextureUsage::DepthWrite))
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
    default:
        SLANG_RHI_ASSERT_FAILURE("Invalid flags");
        return 0;
    }
}

D3D11_FILTER_TYPE translateFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    default:
        return D3D11_FILTER_TYPE(0);

#define CASE(SRC, DST)                                                                                                 \
    case TextureFilteringMode::SRC:                                                                                    \
        return D3D11_FILTER_TYPE_##DST

        CASE(Point, POINT);
        CASE(Linear, LINEAR);

#undef CASE
    }
}

D3D11_FILTER_REDUCTION_TYPE translateFilterReduction(TextureReductionOp op)
{
    switch (op)
    {
    default:
        return D3D11_FILTER_REDUCTION_TYPE(0);

#define CASE(SRC, DST)                                                                                                 \
    case TextureReductionOp::SRC:                                                                                      \
        return D3D11_FILTER_REDUCTION_TYPE_##DST

        CASE(Average, STANDARD);
        CASE(Comparison, COMPARISON);
        CASE(Minimum, MINIMUM);
        CASE(Maximum, MAXIMUM);

#undef CASE
    }
}

D3D11_TEXTURE_ADDRESS_MODE translateAddressingMode(TextureAddressingMode mode)
{
    switch (mode)
    {
    default:
        return D3D11_TEXTURE_ADDRESS_MODE(0);

#define CASE(SRC, DST)                                                                                                 \
    case TextureAddressingMode::SRC:                                                                                   \
        return D3D11_TEXTURE_ADDRESS_##DST

        CASE(Wrap, WRAP);
        CASE(ClampToEdge, CLAMP);
        CASE(ClampToBorder, BORDER);
        CASE(MirrorRepeat, MIRROR);
        CASE(MirrorOnce, MIRROR_ONCE);

#undef CASE
    }
}

D3D11_COMPARISON_FUNC translateComparisonFunc(ComparisonFunc func)
{
    switch (func)
    {
    default:
        // TODO: need to report failures
        return D3D11_COMPARISON_ALWAYS;

#define CASE(FROM, TO)                                                                                                 \
    case ComparisonFunc::FROM:                                                                                         \
        return D3D11_COMPARISON_##TO

        CASE(Never, NEVER);
        CASE(Less, LESS);
        CASE(Equal, EQUAL);
        CASE(LessEqual, LESS_EQUAL);
        CASE(Greater, GREATER);
        CASE(NotEqual, NOT_EQUAL);
        CASE(GreaterEqual, GREATER_EQUAL);
        CASE(Always, ALWAYS);
#undef CASE
    }
}

D3D11_STENCIL_OP translateStencilOp(StencilOp op)
{
    switch (op)
    {
    default:
        // TODO: need to report failures
        return D3D11_STENCIL_OP_KEEP;

#define CASE(FROM, TO)                                                                                                 \
    case StencilOp::FROM:                                                                                              \
        return D3D11_STENCIL_OP_##TO

        CASE(Keep, KEEP);
        CASE(Zero, ZERO);
        CASE(Replace, REPLACE);
        CASE(IncrementSaturate, INCR_SAT);
        CASE(DecrementSaturate, DECR_SAT);
        CASE(Invert, INVERT);
        CASE(IncrementWrap, INCR);
        CASE(DecrementWrap, DECR);
#undef CASE
    }
}

D3D11_FILL_MODE translateFillMode(FillMode mode)
{
    switch (mode)
    {
    default:
        // TODO: need to report failures
        return D3D11_FILL_SOLID;

    case FillMode::Solid:
        return D3D11_FILL_SOLID;
    case FillMode::Wireframe:
        return D3D11_FILL_WIREFRAME;
    }
}

D3D11_CULL_MODE translateCullMode(CullMode mode)
{
    switch (mode)
    {
    default:
        // TODO: need to report failures
        return D3D11_CULL_NONE;

    case CullMode::None:
        return D3D11_CULL_NONE;
    case CullMode::Back:
        return D3D11_CULL_BACK;
    case CullMode::Front:
        return D3D11_CULL_FRONT;
    }
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
    default:
        SLANG_RHI_ASSERT_FAILURE("Unimplemented");
        return (D3D11_BLEND_OP)-1;

#define CASE(FROM, TO)                                                                                                 \
    case BlendOp::FROM:                                                                                                \
        return D3D11_BLEND_OP_##TO
        CASE(Add, ADD);
        CASE(Subtract, SUBTRACT);
        CASE(ReverseSubtract, REV_SUBTRACT);
        CASE(Min, MIN);
        CASE(Max, MAX);
#undef CASE
    }
}

D3D11_BLEND translateBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
    default:
        SLANG_RHI_ASSERT_FAILURE("Unimplemented");
        return (D3D11_BLEND)-1;

#define CASE(FROM, TO)                                                                                                 \
    case BlendFactor::FROM:                                                                                            \
        return D3D11_BLEND_##TO
        CASE(Zero, ZERO);
        CASE(One, ONE);
        CASE(SrcColor, SRC_COLOR);
        CASE(InvSrcColor, INV_SRC_COLOR);
        CASE(SrcAlpha, SRC_ALPHA);
        CASE(InvSrcAlpha, INV_SRC_ALPHA);
        CASE(DestAlpha, DEST_ALPHA);
        CASE(InvDestAlpha, INV_DEST_ALPHA);
        CASE(DestColor, DEST_COLOR);
        CASE(InvDestColor, INV_DEST_ALPHA);
        CASE(SrcAlphaSaturate, SRC_ALPHA_SAT);
        CASE(BlendColor, BLEND_FACTOR);
        CASE(InvBlendColor, INV_BLEND_FACTOR);
        CASE(SecondarySrcColor, SRC1_COLOR);
        CASE(InvSecondarySrcColor, INV_SRC1_COLOR);
        CASE(SecondarySrcAlpha, SRC1_ALPHA);
        CASE(InvSecondarySrcAlpha, INV_SRC1_ALPHA);
#undef CASE
    }
}

D3D11_COLOR_WRITE_ENABLE translateRenderTargetWriteMask(RenderTargetWriteMaskT mask)
{
    UINT result = 0;
#define CASE(FROM, TO)                                                                                                 \
    if (mask & RenderTargetWriteMask::Enable##FROM)                                                                    \
    result |= D3D11_COLOR_WRITE_ENABLE_##TO

    CASE(Red, RED);
    CASE(Green, GREEN);
    CASE(Blue, BLUE);
    CASE(Alpha, ALPHA);

#undef CASE
    return D3D11_COLOR_WRITE_ENABLE(result);
}

void initSrvDesc(const TextureDesc& textureDesc, DXGI_FORMAT pixelFormat, D3D11_SHADER_RESOURCE_VIEW_DESC& descOut)
{
    // create SRV
    descOut = D3D11_SHADER_RESOURCE_VIEW_DESC();

    descOut.Format = (pixelFormat == DXGI_FORMAT_UNKNOWN)
                         ? D3DUtil::calcFormat(D3DUtil::USAGE_SRV, D3DUtil::getMapFormat(textureDesc.format))
                         : pixelFormat;

    switch (textureDesc.type)
    {
    case TextureType::Texture1D:
        if (textureDesc.arrayLength > 1)
        {
            descOut.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
            descOut.Texture1DArray.MostDetailedMip = 0;
            descOut.Texture1DArray.MipLevels = textureDesc.mipLevelCount;
            descOut.Texture1DArray.FirstArraySlice = 0;
            descOut.Texture1DArray.ArraySize = textureDesc.arrayLength;
        }
        else
        {
            descOut.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
            descOut.Texture1D.MostDetailedMip = 0;
            descOut.Texture1D.MipLevels = textureDesc.mipLevelCount;
        }
        break;
    case TextureType::Texture2D:
        if (textureDesc.arrayLength > 1)
        {
            descOut.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            descOut.Texture2DArray.MostDetailedMip = 0;
            descOut.Texture2DArray.MipLevels = textureDesc.mipLevelCount;
            descOut.Texture2DArray.FirstArraySlice = 0;
            descOut.Texture2DArray.ArraySize = textureDesc.arrayLength;
        }
        else
        {
            descOut.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            descOut.Texture2D.MostDetailedMip = 0;
            descOut.Texture2D.MipLevels = textureDesc.mipLevelCount;
        }
        break;
    case TextureType::Texture3D:
        descOut.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        descOut.Texture3D.MostDetailedMip = 0;
        descOut.Texture3D.MipLevels = textureDesc.mipLevelCount;
        break;
    case TextureType::TextureCube:
        if (textureDesc.arrayLength > 1)
        {
            descOut.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
            descOut.TextureCubeArray.MostDetailedMip = 0;
            descOut.TextureCubeArray.MipLevels = textureDesc.mipLevelCount;
            descOut.TextureCubeArray.First2DArrayFace = 0;
            descOut.TextureCubeArray.NumCubes = textureDesc.arrayLength;
        }
        else
        {
            descOut.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            descOut.TextureCube.MostDetailedMip = 0;
            descOut.TextureCube.MipLevels = textureDesc.mipLevelCount;
        }
        break;
    }
}
} // namespace rhi::d3d11

namespace rhi {

Result SLANG_MCALL getD3D11Adapters(std::vector<AdapterInfo>& outAdapters)
{
    std::vector<ComPtr<IDXGIAdapter>> dxgiAdapters;
    SLANG_RETURN_ON_FAIL(D3DUtil::findAdapters(DeviceCheckFlag::UseHardwareDevice, nullptr, dxgiAdapters));

    outAdapters.clear();
    for (const auto& dxgiAdapter : dxgiAdapters)
    {
        DXGI_ADAPTER_DESC desc;
        dxgiAdapter->GetDesc(&desc);
        AdapterInfo info = {};
        auto name = string::from_wstring(desc.Description);
        memcpy(info.name, name.data(), min(name.size(), sizeof(AdapterInfo::name) - 1));
        info.vendorID = desc.VendorId;
        info.deviceID = desc.DeviceId;
        info.luid = D3DUtil::getAdapterLUID(dxgiAdapter);
        outAdapters.push_back(info);
    }
    return SLANG_OK;
}

Result SLANG_MCALL createD3D11Device(const DeviceDesc* desc, IDevice** outDevice)
{
    RefPtr<d3d11::DeviceImpl> result = new d3d11::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi

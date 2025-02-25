#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

bool isSupportedNVAPIOp(IUnknown* dev, uint32_t op);

UINT _calcResourceBindFlags(BufferUsage usage);
UINT _calcResourceBindFlags(TextureUsage usage);
UINT _calcResourceAccessFlags(MemoryType memType);

D3D11_FILTER_TYPE translateFilterMode(TextureFilteringMode mode);
D3D11_FILTER_REDUCTION_TYPE translateFilterReduction(TextureReductionOp op);
D3D11_TEXTURE_ADDRESS_MODE translateAddressingMode(TextureAddressingMode mode);
D3D11_COMPARISON_FUNC translateComparisonFunc(ComparisonFunc func);

D3D11_STENCIL_OP translateStencilOp(StencilOp op);
D3D11_FILL_MODE translateFillMode(FillMode mode);
D3D11_CULL_MODE translateCullMode(CullMode mode);
bool isBlendDisabled(const AspectBlendDesc& desc);
bool isBlendDisabled(const ColorTargetDesc& desc);
D3D11_BLEND_OP translateBlendOp(BlendOp op);
D3D11_BLEND translateBlendFactor(BlendFactor factor);
D3D11_COLOR_WRITE_ENABLE translateRenderTargetWriteMask(RenderTargetWriteMaskT mask);

void initSrvDesc(const TextureDesc& textureDesc, DXGI_FORMAT pixelFormat, D3D11_SHADER_RESOURCE_VIEW_DESC& descOut);

} // namespace rhi::d3d11

namespace rhi {

Result SLANG_MCALL getD3D11Adapters(std::vector<AdapterInfo>& outAdapters);

Result SLANG_MCALL createD3D11Device(const DeviceDesc* desc, IDevice** outDevice);

} // namespace rhi

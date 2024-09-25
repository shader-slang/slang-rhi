#pragma once

#include "wgpu-api.h"

namespace rhi::wgpu {

WGPUTextureFormat translateTextureFormat(Format format);
WGPUVertexFormat translateVertexFormat(Format format);

WGPUBufferUsage translateBufferUsage(BufferUsage usage);
WGPUTextureUsage translateTextureUsage(TextureUsage usage);
WGPUTextureDimension translateTextureDimension(TextureType type);
WGPUTextureViewDimension translateTextureViewDimension(TextureType type, bool array);
WGPUTextureAspect translateTextureAspect(TextureAspect aspect);

WGPUAddressMode translateAddressMode(TextureAddressingMode mode);
WGPUFilterMode translateFilterMode(TextureFilteringMode mode);
WGPUMipmapFilterMode translateMipmapFilterMode(TextureFilteringMode mode);
WGPUCompareFunction translateCompareFunction(ComparisonFunc func);

WGPUPrimitiveTopology translatePrimitiveTopology(PrimitiveTopology topology);
WGPUFrontFace translateFrontFace(FrontFaceMode mode);
WGPUCullMode translateCullMode(CullMode mode);
WGPUStencilOperation translateStencilOp(StencilOp op);
WGPUBlendFactor translateBlendFactor(BlendFactor factor);
WGPUBlendOperation translateBlendOperation(BlendOp op);

WGPULoadOp translateLoadOp(LoadOp op);
WGPUStoreOp translateStoreOp(StoreOp op);

} // namespace rhi::wgpu

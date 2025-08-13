#pragma once

#include <slang-rhi.h>

#include "webgpu-api.h"

namespace rhi::webgpu {

WebGPUTextureFormat translateTextureFormat(Format format);
WebGPUVertexFormat translateVertexFormat(Format format);

WebGPUBufferUsage translateBufferUsage(BufferUsage usage);
WebGPUTextureUsage translateTextureUsage(TextureUsage usage);
WebGPUTextureViewDimension translateTextureViewDimension(TextureType type);
WebGPUTextureAspect translateTextureAspect(TextureAspect aspect);

WebGPUAddressMode translateAddressMode(TextureAddressingMode mode);
WebGPUFilterMode translateFilterMode(TextureFilteringMode mode);
WebGPUMipmapFilterMode translateMipmapFilterMode(TextureFilteringMode mode);
WebGPUCompareFunction translateCompareFunction(ComparisonFunc func);

WebGPUPrimitiveTopology translatePrimitiveTopology(PrimitiveTopology topology);
WebGPUFrontFace translateFrontFace(FrontFaceMode mode);
WebGPUCullMode translateCullMode(CullMode mode);
WebGPUStencilOperation translateStencilOp(StencilOp op);
WebGPUBlendFactor translateBlendFactor(BlendFactor factor);
WebGPUBlendOperation translateBlendOperation(BlendOp op);

WebGPULoadOp translateLoadOp(LoadOp op);
WebGPUStoreOp translateStoreOp(StoreOp op);

} // namespace rhi::webgpu

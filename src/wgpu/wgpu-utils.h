#pragma once

#include "wgpu-api.h"
#include "core/diagnostics.h"

#include <slang-rhi.h>

#include <string>
#include <vector>

namespace rhi::wgpu {

#if !SLANG_WASM
WGPUDawnTogglesDescriptor getDawnTogglesDescriptor();
#endif
Result createWGPUInstance(API& api, WGPUInstance* outInstance);
Result createWGPUAdapter(API& api, WGPUInstance instance, WGPUAdapter* outAdapter);

WGPUStringView translateString(const char* str);
std::string translateString(WGPUStringView str);
const char* getWGPUStatusName(WGPUCreatePipelineAsyncStatus status);
const char* getWGPUStatusName(WGPUWaitStatus status);
const char* getWGPUStatusName(WGPURequestDeviceStatus status);
const char* getWGPUStatusName(WGPUErrorType status);
const char* getWGPUStatusName(WGPUDeviceLostReason status);

template<typename Status>
void reportWGPUError(Device* device, const char* call, Status status, WGPUStringView message, SourceLocation location)
{
    std::string detail = translateString(message);
    reportNativeCallError(device, call, status, getWGPUStatusName(status), location, detail.c_str());
}


WGPUTextureFormat translateTextureFormat(Format format);
WGPUVertexFormat translateVertexFormat(Format format);

WGPUBufferUsage translateBufferUsage(BufferUsage usage);
WGPUTextureUsage translateTextureUsage(TextureUsage usage);
WGPUTextureViewDimension translateTextureViewDimension(TextureType type);
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

struct Context;
WGPUWaitStatus wait(const API& api, WGPUInstance instance, WGPUFuture future, uint64_t timeoutNS = UINT64_MAX);
WGPUWaitStatus wait(Context& ctx, WGPUFuture future, uint64_t timeoutNS = UINT64_MAX);

} // namespace rhi::wgpu

#pragma once

#include "wgpu-api.h"

#include "core/diagnostics.h"

#include <slang-rhi.h>

#include <vector>

namespace rhi::wgpu {

class DeviceImpl;

#if !SLANG_WASM
WGPUDawnTogglesDescriptor getDawnTogglesDescriptor();
#endif
Result createWGPUInstance(API& api, WGPUInstance* outInstance);
Result createWGPUAdapter(API& api, WGPUInstance instance, WGPUAdapter* outAdapter);

const char* getWGPUWaitStatusName(WGPUWaitStatus status);
const char* getWGPURequestAdapterStatusName(WGPURequestAdapterStatus status);
const char* getWGPURequestDeviceStatusName(WGPURequestDeviceStatus status);
const char* getWGPUQueueWorkDoneStatusName(WGPUQueueWorkDoneStatus status);
const char* getWGPUMapAsyncStatusName(WGPUMapAsyncStatus status);
const char* getWGPUSurfaceGetCurrentTextureStatusName(WGPUSurfaceGetCurrentTextureStatus status);

void reportWGPUStatusFailure(
    DeviceImpl* device,
    const char* operation,
    const char* statusName,
    int64_t statusValue,
    const CallSite& callSite,
    const char* detail = nullptr
);

WGPUStringView translateString(const char* str);

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

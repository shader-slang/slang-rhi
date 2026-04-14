#pragma once

#include <slang-rhi.h>

#include "wgpu-api.h"

#include <vector>

namespace rhi::wgpu {

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

inline WGPUDawnTogglesDescriptor getDawnTogglesDescriptor()
{
    // Currently no toggles are needed.
    static const std::vector<const char*> enabledToggles = {};
    static const std::vector<const char*> disabledToggles = {};
    WGPUDawnTogglesDescriptor togglesDesc = {};
    togglesDesc.chain.sType = WGPUSType_DawnTogglesDescriptor;
    togglesDesc.enabledToggleCount = enabledToggles.size();
    togglesDesc.enabledToggles = enabledToggles.data();
    togglesDesc.disabledToggleCount = disabledToggles.size();
    togglesDesc.disabledToggles = disabledToggles.data();
    return togglesDesc;
}

inline Result createWGPUInstance(API& api, WGPUInstance* outInstance)
{
    WGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.capabilities.timedWaitAnyEnable = WGPUBool(true);
    WGPUDawnTogglesDescriptor togglesDesc = getDawnTogglesDescriptor();
    instanceDesc.nextInChain = &togglesDesc.chain;
    WGPUInstance instance = api.wgpuCreateInstance(&instanceDesc);
    if (!instance)
    {
        return SLANG_FAIL;
    }
    *outInstance = instance;
    return SLANG_OK;
}

inline Result createWGPUAdapter(API& api, WGPUInstance instance, WGPUAdapter* outAdapter)
{
    // Request adapter.
    WGPURequestAdapterOptions options = {};
    options.powerPreference = WGPUPowerPreference_HighPerformance;
#if SLANG_WINDOWS_FAMILY
    // TODO: D3D12 Validation errors prevents use of D3D12, use Vulkan for now.
    options.backendType = WGPUBackendType_Vulkan;
#elif SLANG_LINUX_FAMILY
    options.backendType = WGPUBackendType_Vulkan;
#endif
    WGPUDawnTogglesDescriptor togglesDesc = getDawnTogglesDescriptor();
    options.nextInChain = &togglesDesc.chain;

    WGPUAdapter adapter = {};
    {
        WGPURequestAdapterStatus status = WGPURequestAdapterStatus(0);
        WGPURequestAdapterCallbackInfo callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPURequestAdapterStatus status_,
                                   WGPUAdapter adapter_,
                                   WGPUStringView message,
                                   void* userdata1,
                                   void* userdata2)
        {
            *(WGPURequestAdapterStatus*)userdata1 = status_;
            *(WGPUAdapter*)userdata2 = adapter_;
        };
        callbackInfo.userdata1 = &status;
        callbackInfo.userdata2 = &adapter;
        WGPUFuture future = api.wgpuInstanceRequestAdapter(instance, &options, callbackInfo);
        WGPUFutureWaitInfo futures[1] = {{future}};
        uint64_t timeoutNS = UINT64_MAX;
        WGPUWaitStatus waitStatus = api.wgpuInstanceWaitAny(instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
        if (waitStatus != WGPUWaitStatus_Success || status != WGPURequestAdapterStatus_Success)
        {
            return SLANG_FAIL;
        }
    }
    if (!adapter)
    {
        return SLANG_FAIL;
    }
    *outAdapter = adapter;
    return SLANG_OK;
}

} // namespace rhi::wgpu

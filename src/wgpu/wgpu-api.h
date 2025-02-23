#pragma once

#include <slang-rhi.h>

#include "core/platform.h"

#define WGPU_SKIP_DECLARATIONS
#include <dawn/webgpu.h>

// clang-format off

#define SLANG_RHI_WGPU_PROCS(x) \
    x(AdapterInfoFreeMembers) \
    x(AdapterPropertiesMemoryHeapsFreeMembers) \
    x(CreateInstance) \
    x(DrmFormatCapabilitiesFreeMembers) \
    x(GetInstanceFeatures) \
    x(GetProcAddress) \
    x(GetProcAddress2) \
    x(SharedBufferMemoryEndAccessStateFreeMembers) \
    x(SharedTextureMemoryEndAccessStateFreeMembers) \
    x(SurfaceCapabilitiesFreeMembers) \
    /* Procs of Adapter */ \
    x(AdapterCreateDevice) \
    x(AdapterEnumerateFeatures) \
    x(AdapterGetFormatCapabilities) \
    x(AdapterGetInfo) \
    x(AdapterGetInstance) \
    x(AdapterGetLimits) \
    x(AdapterHasFeature) \
    x(AdapterRequestDevice) \
    x(AdapterRequestDevice2) \
    x(AdapterRequestDeviceF) \
    x(AdapterAddRef) \
    x(AdapterRelease) \
    /* Procs of BindGroup */ \
    x(BindGroupSetLabel) \
    x(BindGroupSetLabel2) \
    x(BindGroupAddRef) \
    x(BindGroupRelease) \
    /* Procs of BindGroupLayout */ \
    x(BindGroupLayoutSetLabel) \
    x(BindGroupLayoutSetLabel2) \
    x(BindGroupLayoutAddRef) \
    x(BindGroupLayoutRelease) \
    /* Procs of Buffer */ \
    x(BufferDestroy) \
    x(BufferGetConstMappedRange) \
    x(BufferGetMapState) \
    x(BufferGetMappedRange) \
    x(BufferGetSize) \
    x(BufferGetUsage) \
    x(BufferMapAsync) \
    x(BufferMapAsync2) \
    x(BufferMapAsyncF) \
    x(BufferSetLabel) \
    x(BufferSetLabel2) \
    x(BufferUnmap) \
    x(BufferAddRef) \
    x(BufferRelease) \
    /* Procs of CommandBuffer */ \
    x(CommandBufferSetLabel) \
    x(CommandBufferSetLabel2) \
    x(CommandBufferAddRef) \
    x(CommandBufferRelease) \
    /* Procs of CommandEncoder */ \
    x(CommandEncoderBeginComputePass) \
    x(CommandEncoderBeginRenderPass) \
    x(CommandEncoderClearBuffer) \
    x(CommandEncoderCopyBufferToBuffer) \
    x(CommandEncoderCopyBufferToTexture) \
    x(CommandEncoderCopyTextureToBuffer) \
    x(CommandEncoderCopyTextureToTexture) \
    x(CommandEncoderFinish) \
    x(CommandEncoderInjectValidationError) \
    x(CommandEncoderInjectValidationError2) \
    x(CommandEncoderInsertDebugMarker) \
    x(CommandEncoderInsertDebugMarker2) \
    x(CommandEncoderPopDebugGroup) \
    x(CommandEncoderPushDebugGroup) \
    x(CommandEncoderPushDebugGroup2) \
    x(CommandEncoderResolveQuerySet) \
    x(CommandEncoderSetLabel) \
    x(CommandEncoderSetLabel2) \
    x(CommandEncoderWriteBuffer) \
    x(CommandEncoderWriteTimestamp) \
    x(CommandEncoderAddRef) \
    x(CommandEncoderRelease) \
    /* Procs of ComputePassEncoder */ \
    x(ComputePassEncoderDispatchWorkgroups) \
    x(ComputePassEncoderDispatchWorkgroupsIndirect) \
    x(ComputePassEncoderEnd) \
    x(ComputePassEncoderInsertDebugMarker) \
    x(ComputePassEncoderInsertDebugMarker2) \
    x(ComputePassEncoderPopDebugGroup) \
    x(ComputePassEncoderPushDebugGroup) \
    x(ComputePassEncoderPushDebugGroup2) \
    x(ComputePassEncoderSetBindGroup) \
    x(ComputePassEncoderSetLabel) \
    x(ComputePassEncoderSetLabel2) \
    x(ComputePassEncoderSetPipeline) \
    x(ComputePassEncoderWriteTimestamp) \
    x(ComputePassEncoderAddRef) \
    x(ComputePassEncoderRelease) \
    /* Procs of ComputePipeline */ \
    x(ComputePipelineGetBindGroupLayout) \
    x(ComputePipelineSetLabel) \
    x(ComputePipelineSetLabel2) \
    x(ComputePipelineAddRef) \
    x(ComputePipelineRelease) \
    /* Procs of Device */ \
    x(DeviceCreateBindGroup) \
    x(DeviceCreateBindGroupLayout) \
    x(DeviceCreateBuffer) \
    x(DeviceCreateCommandEncoder) \
    x(DeviceCreateComputePipeline) \
    x(DeviceCreateComputePipelineAsync) \
    x(DeviceCreateComputePipelineAsync2) \
    x(DeviceCreateComputePipelineAsyncF) \
    x(DeviceCreateErrorBuffer) \
    x(DeviceCreateErrorExternalTexture) \
    x(DeviceCreateErrorShaderModule) \
    x(DeviceCreateErrorShaderModule2) \
    x(DeviceCreateErrorTexture) \
    x(DeviceCreateExternalTexture) \
    x(DeviceCreatePipelineLayout) \
    x(DeviceCreateQuerySet) \
    x(DeviceCreateRenderBundleEncoder) \
    x(DeviceCreateRenderPipeline) \
    x(DeviceCreateRenderPipelineAsync) \
    x(DeviceCreateRenderPipelineAsync2) \
    x(DeviceCreateRenderPipelineAsyncF) \
    x(DeviceCreateSampler) \
    x(DeviceCreateShaderModule) \
    x(DeviceCreateSwapChain) \
    x(DeviceCreateTexture) \
    x(DeviceDestroy) \
    x(DeviceEnumerateFeatures) \
    x(DeviceForceLoss) \
    x(DeviceForceLoss2) \
    x(DeviceGetAHardwareBufferProperties) \
    x(DeviceGetAdapter) \
    x(DeviceGetLimits) \
    x(DeviceGetQueue) \
    x(DeviceGetSupportedSurfaceUsage) \
    x(DeviceHasFeature) \
    x(DeviceImportSharedBufferMemory) \
    x(DeviceImportSharedFence) \
    x(DeviceImportSharedTextureMemory) \
    x(DeviceInjectError) \
    x(DeviceInjectError2) \
    x(DevicePopErrorScope) \
    x(DevicePopErrorScope2) \
    x(DevicePopErrorScopeF) \
    x(DevicePushErrorScope) \
    x(DeviceSetDeviceLostCallback) \
    x(DeviceSetLabel) \
    x(DeviceSetLabel2) \
    x(DeviceSetLoggingCallback) \
    x(DeviceSetUncapturedErrorCallback) \
    x(DeviceTick) \
    x(DeviceValidateTextureDescriptor) \
    x(DeviceAddRef) \
    x(DeviceRelease) \
    /* Procs of ExternalTexture */ \
    x(ExternalTextureDestroy) \
    x(ExternalTextureExpire) \
    x(ExternalTextureRefresh) \
    x(ExternalTextureSetLabel) \
    x(ExternalTextureSetLabel2) \
    x(ExternalTextureAddRef) \
    x(ExternalTextureRelease) \
    /* Procs of Instance */ \
    x(InstanceCreateSurface) \
    x(InstanceEnumerateWGSLLanguageFeatures) \
    x(InstanceHasWGSLLanguageFeature) \
    x(InstanceProcessEvents) \
    x(InstanceRequestAdapter) \
    x(InstanceRequestAdapter2) \
    x(InstanceRequestAdapterF) \
    x(InstanceWaitAny) \
    x(InstanceAddRef) \
    x(InstanceRelease) \
    /* Procs of PipelineLayout */ \
    x(PipelineLayoutSetLabel) \
    x(PipelineLayoutSetLabel2) \
    x(PipelineLayoutAddRef) \
    x(PipelineLayoutRelease) \
    /* Procs of QuerySet */ \
    x(QuerySetDestroy) \
    x(QuerySetGetCount) \
    x(QuerySetGetType) \
    x(QuerySetSetLabel) \
    x(QuerySetSetLabel2) \
    x(QuerySetAddRef) \
    x(QuerySetRelease) \
    /* Procs of Queue */ \
    x(QueueCopyExternalTextureForBrowser) \
    x(QueueCopyTextureForBrowser) \
    x(QueueOnSubmittedWorkDone) \
    x(QueueOnSubmittedWorkDone2) \
    x(QueueOnSubmittedWorkDoneF) \
    x(QueueSetLabel) \
    x(QueueSetLabel2) \
    x(QueueSubmit) \
    x(QueueWriteBuffer) \
    x(QueueWriteTexture) \
    x(QueueAddRef) \
    x(QueueRelease) \
    /* Procs of RenderBundle */ \
    x(RenderBundleSetLabel) \
    x(RenderBundleSetLabel2) \
    x(RenderBundleAddRef) \
    x(RenderBundleRelease) \
    /* Procs of RenderBundleEncoder */ \
    x(RenderBundleEncoderDraw) \
    x(RenderBundleEncoderDrawIndexed) \
    x(RenderBundleEncoderDrawIndexedIndirect) \
    x(RenderBundleEncoderDrawIndirect) \
    x(RenderBundleEncoderFinish) \
    x(RenderBundleEncoderInsertDebugMarker) \
    x(RenderBundleEncoderInsertDebugMarker2) \
    x(RenderBundleEncoderPopDebugGroup) \
    x(RenderBundleEncoderPushDebugGroup) \
    x(RenderBundleEncoderPushDebugGroup2) \
    x(RenderBundleEncoderSetBindGroup) \
    x(RenderBundleEncoderSetIndexBuffer) \
    x(RenderBundleEncoderSetLabel) \
    x(RenderBundleEncoderSetLabel2) \
    x(RenderBundleEncoderSetPipeline) \
    x(RenderBundleEncoderSetVertexBuffer) \
    x(RenderBundleEncoderAddRef) \
    x(RenderBundleEncoderRelease) \
    /* Procs of RenderPassEncoder */ \
    x(RenderPassEncoderBeginOcclusionQuery) \
    x(RenderPassEncoderDraw) \
    x(RenderPassEncoderDrawIndexed) \
    x(RenderPassEncoderDrawIndexedIndirect) \
    x(RenderPassEncoderDrawIndirect) \
    x(RenderPassEncoderEnd) \
    x(RenderPassEncoderEndOcclusionQuery) \
    x(RenderPassEncoderExecuteBundles) \
    x(RenderPassEncoderInsertDebugMarker) \
    x(RenderPassEncoderInsertDebugMarker2) \
    x(RenderPassEncoderMultiDrawIndexedIndirect) \
    x(RenderPassEncoderMultiDrawIndirect) \
    x(RenderPassEncoderPixelLocalStorageBarrier) \
    x(RenderPassEncoderPopDebugGroup) \
    x(RenderPassEncoderPushDebugGroup) \
    x(RenderPassEncoderPushDebugGroup2) \
    x(RenderPassEncoderSetBindGroup) \
    x(RenderPassEncoderSetBlendConstant) \
    x(RenderPassEncoderSetIndexBuffer) \
    x(RenderPassEncoderSetLabel) \
    x(RenderPassEncoderSetLabel2) \
    x(RenderPassEncoderSetPipeline) \
    x(RenderPassEncoderSetScissorRect) \
    x(RenderPassEncoderSetStencilReference) \
    x(RenderPassEncoderSetVertexBuffer) \
    x(RenderPassEncoderSetViewport) \
    x(RenderPassEncoderWriteTimestamp) \
    x(RenderPassEncoderAddRef) \
    x(RenderPassEncoderRelease) \
    /* Procs of RenderPipeline */ \
    x(RenderPipelineGetBindGroupLayout) \
    x(RenderPipelineSetLabel) \
    x(RenderPipelineSetLabel2) \
    x(RenderPipelineAddRef) \
    x(RenderPipelineRelease) \
    /* Procs of Sampler */ \
    x(SamplerSetLabel) \
    x(SamplerSetLabel2) \
    x(SamplerAddRef) \
    x(SamplerRelease) \
    /* Procs of ShaderModule */ \
    x(ShaderModuleGetCompilationInfo) \
    x(ShaderModuleGetCompilationInfo2) \
    x(ShaderModuleGetCompilationInfoF) \
    x(ShaderModuleSetLabel) \
    x(ShaderModuleSetLabel2) \
    x(ShaderModuleAddRef) \
    x(ShaderModuleRelease) \
    /* Procs of SharedBufferMemory */ \
    x(SharedBufferMemoryBeginAccess) \
    x(SharedBufferMemoryCreateBuffer) \
    x(SharedBufferMemoryEndAccess) \
    x(SharedBufferMemoryGetProperties) \
    x(SharedBufferMemoryIsDeviceLost) \
    x(SharedBufferMemorySetLabel) \
    x(SharedBufferMemorySetLabel2) \
    x(SharedBufferMemoryAddRef) \
    x(SharedBufferMemoryRelease) \
    /* Procs of SharedFence */ \
    x(SharedFenceExportInfo) \
    x(SharedFenceAddRef) \
    x(SharedFenceRelease) \
    /* Procs of SharedTextureMemory */ \
    x(SharedTextureMemoryBeginAccess) \
    x(SharedTextureMemoryCreateTexture) \
    x(SharedTextureMemoryEndAccess) \
    x(SharedTextureMemoryGetProperties) \
    x(SharedTextureMemoryIsDeviceLost) \
    x(SharedTextureMemorySetLabel) \
    x(SharedTextureMemorySetLabel2) \
    x(SharedTextureMemoryAddRef) \
    x(SharedTextureMemoryRelease) \
    /* Procs of Surface */ \
    x(SurfaceConfigure) \
    x(SurfaceGetCapabilities) \
    x(SurfaceGetCurrentTexture) \
    x(SurfaceGetPreferredFormat) \
    x(SurfacePresent) \
    x(SurfaceSetLabel) \
    x(SurfaceSetLabel2) \
    x(SurfaceUnconfigure) \
    x(SurfaceAddRef) \
    x(SurfaceRelease) \
    /* Procs of SwapChain */ \
    x(SwapChainGetCurrentTexture) \
    x(SwapChainGetCurrentTextureView) \
    x(SwapChainPresent) \
    x(SwapChainAddRef) \
    x(SwapChainRelease) \
    /* Procs of Texture */ \
    x(TextureCreateErrorView) \
    x(TextureCreateView) \
    x(TextureDestroy) \
    x(TextureGetDepthOrArrayLayers) \
    x(TextureGetDimension) \
    x(TextureGetFormat) \
    x(TextureGetHeight) \
    x(TextureGetMipLevelCount) \
    x(TextureGetSampleCount) \
    x(TextureGetUsage) \
    x(TextureGetWidth) \
    x(TextureSetLabel) \
    x(TextureSetLabel2) \
    x(TextureAddRef) \
    x(TextureRelease) \
    /* Procs of TextureView */ \
    x(TextureViewSetLabel) \
    x(TextureViewSetLabel2) \
    x(TextureViewAddRef) \
    x(TextureViewRelease)

// clang-format on

namespace rhi::wgpu {

struct API
{
    SharedLibraryHandle m_module = nullptr;

    ~API();

    Result init();

#define WGPU_DECLARE_PROC(name) WGPUProc##name wgpu##name;
    SLANG_RHI_WGPU_PROCS(WGPU_DECLARE_PROC)
#undef WGPU_DECLARE_PROC
};

} // namespace rhi::wgpu

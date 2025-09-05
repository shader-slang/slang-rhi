#pragma once

#include <slang-rhi.h>

#include "core/platform.h"

#define WGPU_SKIP_DECLARATIONS
#include <dawn/webgpu.h>

// clang-format off

#define SLANG_RHI_WGPU_PROCS(x) \
    x(AdapterInfoFreeMembers) \
    x(AdapterPropertiesMemoryHeapsFreeMembers) \
    x(AdapterPropertiesSubgroupMatrixConfigsFreeMembers) \
    x(CreateInstance) \
    x(DawnDrmFormatCapabilitiesFreeMembers) \
    x(GetInstanceCapabilities) \
    x(GetProcAddress) \
    x(SharedBufferMemoryEndAccessStateFreeMembers) \
    x(SharedTextureMemoryEndAccessStateFreeMembers) \
    x(SupportedWGSLLanguageFeaturesFreeMembers) \
    x(SupportedFeaturesFreeMembers) \
    x(SurfaceCapabilitiesFreeMembers) \
    /* Procs of Adapter */ \
    x(AdapterCreateDevice) \
    x(AdapterGetFeatures) \
    x(AdapterGetFormatCapabilities) \
    x(AdapterGetInfo) \
    x(AdapterGetInstance) \
    x(AdapterGetLimits) \
    x(AdapterHasFeature) \
    x(AdapterRequestDevice) \
    x(AdapterAddRef) \
    x(AdapterRelease) \
    /* Procs of BindGroup */ \
    x(BindGroupSetLabel) \
    x(BindGroupAddRef) \
    x(BindGroupRelease) \
    /* Procs of BindGroupLayout */ \
    x(BindGroupLayoutSetLabel) \
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
    x(BufferReadMappedRange) \
    x(BufferSetLabel) \
    x(BufferUnmap) \
    x(BufferWriteMappedRange) \
    x(BufferAddRef) \
    x(BufferRelease) \
    /* Procs of CommandBuffer */ \
    x(CommandBufferSetLabel) \
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
    x(CommandEncoderInsertDebugMarker) \
    x(CommandEncoderPopDebugGroup) \
    x(CommandEncoderPushDebugGroup) \
    x(CommandEncoderResolveQuerySet) \
    x(CommandEncoderSetLabel) \
    x(CommandEncoderWriteBuffer) \
    x(CommandEncoderWriteTimestamp) \
    x(CommandEncoderAddRef) \
    x(CommandEncoderRelease) \
    /* Procs of ComputePassEncoder */ \
    x(ComputePassEncoderDispatchWorkgroups) \
    x(ComputePassEncoderDispatchWorkgroupsIndirect) \
    x(ComputePassEncoderEnd) \
    x(ComputePassEncoderInsertDebugMarker) \
    x(ComputePassEncoderPopDebugGroup) \
    x(ComputePassEncoderPushDebugGroup) \
    x(ComputePassEncoderSetBindGroup) \
    x(ComputePassEncoderSetImmediateData) \
    x(ComputePassEncoderSetLabel) \
    x(ComputePassEncoderSetPipeline) \
    x(ComputePassEncoderWriteTimestamp) \
    x(ComputePassEncoderAddRef) \
    x(ComputePassEncoderRelease) \
    /* Procs of ComputePipeline */ \
    x(ComputePipelineGetBindGroupLayout) \
    x(ComputePipelineSetLabel) \
    x(ComputePipelineAddRef) \
    x(ComputePipelineRelease) \
    /* Procs of Device */ \
    x(DeviceCreateBindGroup) \
    x(DeviceCreateBindGroupLayout) \
    x(DeviceCreateBuffer) \
    x(DeviceCreateCommandEncoder) \
    x(DeviceCreateComputePipeline) \
    x(DeviceCreateComputePipelineAsync) \
    x(DeviceCreateErrorBuffer) \
    x(DeviceCreateErrorExternalTexture) \
    x(DeviceCreateErrorShaderModule) \
    x(DeviceCreateErrorTexture) \
    x(DeviceCreateExternalTexture) \
    x(DeviceCreatePipelineLayout) \
    x(DeviceCreateQuerySet) \
    x(DeviceCreateRenderBundleEncoder) \
    x(DeviceCreateRenderPipeline) \
    x(DeviceCreateRenderPipelineAsync) \
    x(DeviceCreateSampler) \
    x(DeviceCreateShaderModule) \
    x(DeviceCreateTexture) \
    x(DeviceDestroy) \
    x(DeviceForceLoss) \
    x(DeviceGetAHardwareBufferProperties) \
    x(DeviceGetAdapter) \
    x(DeviceGetAdapterInfo) \
    x(DeviceGetFeatures) \
    x(DeviceGetLimits) \
    x(DeviceGetLostFuture) \
    x(DeviceGetQueue) \
    x(DeviceHasFeature) \
    x(DeviceImportSharedBufferMemory) \
    x(DeviceImportSharedFence) \
    x(DeviceImportSharedTextureMemory) \
    x(DeviceInjectError) \
    x(DevicePopErrorScope) \
    x(DevicePushErrorScope) \
    x(DeviceSetLabel) \
    x(DeviceSetLoggingCallback) \
    x(DeviceTick) \
    x(DeviceValidateTextureDescriptor) \
    x(DeviceAddRef) \
    x(DeviceRelease) \
    /* Procs of ExternalTexture */ \
    x(ExternalTextureDestroy) \
    x(ExternalTextureExpire) \
    x(ExternalTextureRefresh) \
    x(ExternalTextureSetLabel) \
    x(ExternalTextureAddRef) \
    x(ExternalTextureRelease) \
    /* Procs of Instance */ \
    x(InstanceCreateSurface) \
    x(InstanceGetWGSLLanguageFeatures) \
    x(InstanceHasWGSLLanguageFeature) \
    x(InstanceProcessEvents) \
    x(InstanceRequestAdapter) \
    x(InstanceWaitAny) \
    x(InstanceAddRef) \
    x(InstanceRelease) \
    /* Procs of PipelineLayout */ \
    x(PipelineLayoutSetLabel) \
    x(PipelineLayoutAddRef) \
    x(PipelineLayoutRelease) \
    /* Procs of QuerySet */ \
    x(QuerySetDestroy) \
    x(QuerySetGetCount) \
    x(QuerySetGetType) \
    x(QuerySetSetLabel) \
    x(QuerySetAddRef) \
    x(QuerySetRelease) \
    /* Procs of Queue */ \
    x(QueueCopyExternalTextureForBrowser) \
    x(QueueCopyTextureForBrowser) \
    x(QueueOnSubmittedWorkDone) \
    x(QueueSetLabel) \
    x(QueueSubmit) \
    x(QueueWriteBuffer) \
    x(QueueWriteTexture) \
    x(QueueAddRef) \
    x(QueueRelease) \
    /* Procs of RenderBundle */ \
    x(RenderBundleSetLabel) \
    x(RenderBundleAddRef) \
    x(RenderBundleRelease) \
    /* Procs of RenderBundleEncoder */ \
    x(RenderBundleEncoderDraw) \
    x(RenderBundleEncoderDrawIndexed) \
    x(RenderBundleEncoderDrawIndexedIndirect) \
    x(RenderBundleEncoderDrawIndirect) \
    x(RenderBundleEncoderFinish) \
    x(RenderBundleEncoderInsertDebugMarker) \
    x(RenderBundleEncoderPopDebugGroup) \
    x(RenderBundleEncoderPushDebugGroup) \
    x(RenderBundleEncoderSetBindGroup) \
    x(RenderBundleEncoderSetImmediateData) \
    x(RenderBundleEncoderSetIndexBuffer) \
    x(RenderBundleEncoderSetLabel) \
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
    x(RenderPassEncoderMultiDrawIndexedIndirect) \
    x(RenderPassEncoderMultiDrawIndirect) \
    x(RenderPassEncoderPixelLocalStorageBarrier) \
    x(RenderPassEncoderPopDebugGroup) \
    x(RenderPassEncoderPushDebugGroup) \
    x(RenderPassEncoderSetBindGroup) \
    x(RenderPassEncoderSetBlendConstant) \
    x(RenderPassEncoderSetImmediateData) \
    x(RenderPassEncoderSetIndexBuffer) \
    x(RenderPassEncoderSetLabel) \
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
    x(RenderPipelineAddRef) \
    x(RenderPipelineRelease) \
    /* Procs of Sampler */ \
    x(SamplerSetLabel) \
    x(SamplerAddRef) \
    x(SamplerRelease) \
    /* Procs of ShaderModule */ \
    x(ShaderModuleGetCompilationInfo) \
    x(ShaderModuleSetLabel) \
    x(ShaderModuleAddRef) \
    x(ShaderModuleRelease) \
    /* Procs of SharedBufferMemory */ \
    x(SharedBufferMemoryBeginAccess) \
    x(SharedBufferMemoryCreateBuffer) \
    x(SharedBufferMemoryEndAccess) \
    x(SharedBufferMemoryGetProperties) \
    x(SharedBufferMemoryIsDeviceLost) \
    x(SharedBufferMemorySetLabel) \
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
    x(SharedTextureMemoryAddRef) \
    x(SharedTextureMemoryRelease) \
    /* Procs of Surface */ \
    x(SurfaceConfigure) \
    x(SurfaceGetCapabilities) \
    x(SurfaceGetCurrentTexture) \
    x(SurfacePresent) \
    x(SurfaceSetLabel) \
    x(SurfaceUnconfigure) \
    x(SurfaceAddRef) \
    x(SurfaceRelease) \
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
    x(TextureAddRef) \
    x(TextureRelease) \
    /* Procs of TextureView */ \
    x(TextureViewSetLabel) \
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

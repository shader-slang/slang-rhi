#pragma once

#include "metal-base.h"

namespace rhi::metal {

// A Metal bindless DescriptorHandle carries the resource's raw native id (gpuAddress /
// gpuResourceID) directly, so there is no descriptor heap, slot allocation, or per-handle
// residency tracking; keeping the resource alive is the application's responsibility. Handles
// are recomputed from the stable native id on every call (no caching, unlike the vk/d3d12 sets).
//
// GPU residency is not tracked per handle: on the default residency-set path (GPUFamilyApple6+)
// every created resource is resident, so bindless references just work; the per-encoder
// useResource fallback (SLANG_RHI_METAL_NO_RESIDENCY_SET / no residency-set support) only makes
// resources reachable through the argument-buffer binding path resident, so a resource referenced
// solely via a bindless handle relies on the residency-set path.
//
// Combined texture+sampler is intentionally not supported (no override, inherits the base
// SLANG_E_NOT_AVAILABLE): it would need two 64-bit gpuResourceIDs (128 bits), which do not fit a
// single 64-bit DescriptorHandle::value (see shader-slang/slang#11540).
class BindlessDescriptorSet : public RefObject
{
public:
    BindlessDescriptorSet(DeviceImpl* device, const BindlessDesc& desc);

    Result initialize();

    Result allocBufferHandle(
        IBuffer* buffer,
        DescriptorHandleAccess access,
        Format format,
        BufferRange range,
        DescriptorHandle* outHandle
    );
    Result allocTextureHandle(ITextureView* textureView, DescriptorHandleAccess access, DescriptorHandle* outHandle);
    Result allocSamplerHandle(ISampler* sampler, DescriptorHandle* outHandle);
    Result allocAccelerationStructureHandle(IAccelerationStructure* accelerationStructure, DescriptorHandle* outHandle);
};

} // namespace rhi::metal

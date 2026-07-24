#pragma once

#include "metal-base.h"

namespace rhi::metal {

// A Metal bindless DescriptorHandle carries the resource's raw native id (gpuAddress /
// gpuResourceID) directly, so there is no descriptor heap, slot allocation, or per-handle
// residency tracking; keeping the resource alive is the application's responsibility.
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

public:
    DeviceImpl* m_device;
    BindlessDesc m_desc;
};

} // namespace rhi::metal

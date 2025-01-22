#pragma once

#include <slang-rhi.h>

namespace rhi {

inline AccelerationStructureInstanceDescType getAccelerationStructureInstanceDescType(DeviceType deviceType)
{
    switch (deviceType)
    {
    case DeviceType::D3D12:
        return AccelerationStructureInstanceDescType::D3D12;
    case DeviceType::Vulkan:
        return AccelerationStructureInstanceDescType::Vulkan;
    case DeviceType::Metal:
        return AccelerationStructureInstanceDescType::Metal;
    case DeviceType::CUDA:
        return AccelerationStructureInstanceDescType::Optix;
    }
    return AccelerationStructureInstanceDescType::Generic;
}

inline AccelerationStructureInstanceDescType getAccelerationStructureInstanceDescType(IDevice* device)
{
    return getAccelerationStructureInstanceDescType(device->getDeviceInfo().deviceType);
}

inline size_t getAccelerationStructureInstanceDescSize(AccelerationStructureInstanceDescType type)
{
    switch (type)
    {
    case AccelerationStructureInstanceDescType::Generic:
        return sizeof(AccelerationStructureInstanceDescGeneric);
    case AccelerationStructureInstanceDescType::D3D12:
        return sizeof(AccelerationStructureInstanceDescD3D12);
    case AccelerationStructureInstanceDescType::Vulkan:
        return sizeof(AccelerationStructureInstanceDescVulkan);
    case AccelerationStructureInstanceDescType::Optix:
        return sizeof(AccelerationStructureInstanceDescOptix);
    case AccelerationStructureInstanceDescType::Metal:
        return sizeof(AccelerationStructureInstanceDescMetal);
    }
    return 0;
}

inline void convertAccelerationStructureInstanceDesc(
    AccelerationStructureInstanceDescType dstType,
    void* dst,
    const AccelerationStructureInstanceDescGeneric* src
)
{
    switch (dstType)
    {
    case AccelerationStructureInstanceDescType::Generic:
        ::memcpy(dst, src, sizeof(AccelerationStructureInstanceDescGeneric));
        break;
    case AccelerationStructureInstanceDescType::D3D12:
    {
        static_assert(
            sizeof(AccelerationStructureInstanceDescD3D12) == sizeof(AccelerationStructureInstanceDescGeneric)
        );
        auto dstD3D12 = reinterpret_cast<AccelerationStructureInstanceDescD3D12*>(dst);
        ::memcpy(dstD3D12, src, sizeof(AccelerationStructureInstanceDescD3D12));
        break;
    }
    case AccelerationStructureInstanceDescType::Vulkan:
    {
        static_assert(
            sizeof(AccelerationStructureInstanceDescVulkan) == sizeof(AccelerationStructureInstanceDescGeneric)
        );
        auto dstVulkan = reinterpret_cast<AccelerationStructureInstanceDescVulkan*>(dst);
        ::memcpy(dstVulkan, src, sizeof(AccelerationStructureInstanceDescVulkan));
        break;
    }
    case AccelerationStructureInstanceDescType::Optix:
    {
        auto dstOptix = reinterpret_cast<AccelerationStructureInstanceDescOptix*>(dst);
        ::memcpy(dstOptix->transform, src->transform, 4 * 3 * sizeof(float));
        dstOptix->instanceId = src->instanceID;
        dstOptix->sbtOffset = src->instanceContributionToHitGroupIndex;
        dstOptix->visibilityMask = src->instanceMask;
        // Generic flags match the Optix flags.
        // TriangleFacingCullDisable -> OPTIX_INSTANCE_FLAG_DISABLE_TRIANGLE_FACE_CULLING
        // TriangleFrontCounterClockwise -> OPTIX_INSTANCE_FLAG_FLIP_TRIANGLE_FACING
        // ForceOpaque -> OPTIX_INSTANCE_FLAG_DISABLE_ANYHIT
        // NoOpaque -> OPTIX_INSTANCE_FLAG_ENFORCE_ANYHIT
        dstOptix->flags = (uint32_t)src->flags;
        dstOptix->traversableHandle = src->accelerationStructure.value;
        dstOptix->pad[0] = 0;
        dstOptix->pad[1] = 0;
        break;
    }
    case AccelerationStructureInstanceDescType::Metal:
    {
        auto dstMetal = reinterpret_cast<AccelerationStructureInstanceDescMetal*>(dst);
        // Transpose the transform matrix.
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                dstMetal->transform[j][i] = src->transform[i][j];
            }
        }
        // Generic flags match the Metal options.
        // TriangleFacingCullDisable -> DisableTriangleCulling
        // TriangleFrontCounterClockwise -> TriangleFrontFacingWindingCounterClockwise
        // ForceOpaque -> Opaque
        // NoOpaque -> NonOpaque
        dstMetal->options = (uint32_t)src->flags;
        dstMetal->mask = src->instanceMask;
        dstMetal->intersectionFunctionTableOffset = src->instanceContributionToHitGroupIndex;
        dstMetal->accelerationStructureIndex = src->accelerationStructure.value;
        dstMetal->userID = src->instanceID;
        break;
    }
    }
}

inline void convertAccelerationStructureInstanceDescs(
    size_t count,
    AccelerationStructureInstanceDescType dstType,
    void* dst,
    size_t dstStride,
    const AccelerationStructureInstanceDescGeneric* src,
    size_t srcStride
)
{
    if (dstType == AccelerationStructureInstanceDescType::D3D12 ||
        dstType == AccelerationStructureInstanceDescType::Vulkan)
    {
        // D3D12/Vulkan descriptor are compatible with the generic descriptor.
        ::memcpy(dst, src, count * sizeof(AccelerationStructureInstanceDescGeneric));
        return;
    }

    for (size_t i = 0; i < count; ++i)
    {
        convertAccelerationStructureInstanceDesc(dstType, dst, src);
        dst = (uint8_t*)dst + dstStride;
        src = (const AccelerationStructureInstanceDescGeneric*)((const uint8_t*)src + srcStride);
    }
}

} // namespace rhi

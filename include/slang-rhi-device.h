// slang-rhi definitions shared between C++ and Slang.
// This file is included from slang-rhi.h by default.
// Slang shaders can include this file directly.

#if __cplusplus
#include <cstdint>
#endif

namespace rhi {

typedef uint64_t GpuVirtualAddress;

struct IndirectDrawArguments
{
    uint32_t vertexCountPerInstance;
    uint32_t instanceCount;
    uint32_t startVertexLocation;
    uint32_t startInstanceLocation;
};

struct IndirectDrawIndexedArguments
{
    uint32_t indexCountPerInstance;
    uint32_t instanceCount;
    uint32_t startIndexLocation;
    int32_t baseVertexLocation;
    uint32_t startInstanceLocation;
};

struct IndirectDispatchArguments
{
    uint32_t threadGroupCountX;
    uint32_t threadGroupCountY;
    uint32_t threadGroupCountZ;
};

/// Instance descriptor matching D3D12_RAYTRACING_INSTANCE_DESC.
struct AccelerationStructureInstanceDescD3D12
{
    float Transform[3][4];
    uint32_t InstanceID : 24;
    uint32_t InstanceMask : 8;
    uint32_t InstanceContributionToHitGroupIndex : 24;
    uint32_t Flags : 8;
    GpuVirtualAddress AccelerationStructure;
};

/// Instance descriptor matching VkAccelerationStructureInstanceKHR.
struct AccelerationStructureInstanceDescVulkan
{
    float transform[4][3];
    uint32_t instanceCustomIndex : 24;
    uint32_t mask : 8;
    uint32_t instanceShaderBindingTableRecordOffset : 24;
    uint32_t flags : 8;
    uint64_t accelerationStructureReference;
};

/// Instance descriptor matching OptixInstance.
struct AccelerationStructureInstanceDescOptix
{
    float transform[3][4];
    uint32_t instanceId;
    uint32_t sbtOffset;
    uint32_t visibilityMask;
    uint32_t flags;
    uint64_t traversableHandle;
    uint32_t pad[2];
};

/// Instance descriptor matching MTLAccelerationStructureUserIDInstanceDescriptor.
struct AccelerationStructureInstanceDescMetal
{
    float transform[4][3];
    uint32_t options;
    uint32_t mask;
    uint32_t intersectionFunctionTableOffset;
    uint32_t accelerationStructureIndex;
    uint32_t userID;
};

} // namespace rhi

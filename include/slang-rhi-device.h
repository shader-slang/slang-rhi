// slang-rhi definitions shared between C++ and Slang.
// This file is included from slang-rhi.h by default.
// Slang shaders can include this file directly.

#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
#endif

namespace rhi {

/// Virtual address in the GPU address space.
typedef uint64_t DeviceAddress;

// ----------------------------------------------------------------------------
// Indirect argument structures
// ----------------------------------------------------------------------------

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

// ----------------------------------------------------------------------------
// Acceleration structure instance descriptors
// ----------------------------------------------------------------------------

/// Instance descriptor matching D3D12_RAYTRACING_INSTANCE_DESC.
struct AccelerationStructureInstanceDescD3D12
{
    float Transform[3][4];
    uint32_t InstanceID : 24;
    uint32_t InstanceMask : 8;
    uint32_t InstanceContributionToHitGroupIndex : 24;
    uint32_t Flags : 8;
    DeviceAddress AccelerationStructure;
};

enum AccelerationStructureMotionInstanceTypeVulkan
{
    Static = 0,
    Matrix = 1,
    SRT = 2
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

/// Instance descriptor matching VkAccelerationStructureMatrixMotionInstanceNV.
struct AccelerationStructureMatrixMotionInstanceDescVulkan
{
    float transformT0[3][4];
    float transformT1[3][4];
    uint32_t instanceCustomIndex : 24;
    uint32_t mask : 8;
    uint32_t instanceShaderBindingTableRecordOffset : 24;
    uint32_t flags : 8;
    uint64_t accelerationStructureReference;
};

/// Matrix motion instance descriptor matching VkAccelerationStructureMatrixMotionInstanceNV.
/// SRT (Scale-Rotation-Translation) transformation data matching VkSRTDataNV.
struct SRTDataVulkan
{
    float sx;
    float a;
    float b;
    float pvx;
    float sy;
    float c;
    float pvy;
    float sz;
    float pvz;
    float qx;
    float qy;
    float qz;
    float qw;
    float tx;
    float ty;
    float tz;
};

/// SRT motion instance descriptor matching VkAccelerationStructureSRTMotionInstanceNV.
struct AccelerationStructureSRTMotionInstanceDescVulkan
{
    SRTDataVulkan transformT0;
    SRTDataVulkan transformT1;
    uint32_t instanceCustomIndex : 24;
    uint32_t mask : 8;
    uint32_t instanceShaderBindingTableRecordOffset : 24;
    uint32_t flags : 8;
    uint64_t accelerationStructureReference;
};

// The Vulkan headers define a union for the motion instance data, but Slang doesn't support unions,
// so we have to use separate structs for each type of motion instance.

struct AccelerationStructureStaticMotionInstanceVulkan
{
    uint32_t type;  // VkAccelerationStructureMotionInstanceTypeNV
    uint32_t flags; // VkAccelerationStructureMotionInstanceFlagsNV
    AccelerationStructureInstanceDescVulkan staticInstance;
    uint8_t padding[88];
};

struct AccelerationStructureMatrixMotionInstanceVulkan
{
    uint32_t type;  // VkAccelerationStructureMotionInstanceTypeNV
    uint32_t flags; // VkAccelerationStructureMotionInstanceFlagsNV
    AccelerationStructureMatrixMotionInstanceDescVulkan matrixMotionInstance;
    uint8_t padding[40];
};

struct AccelerationStructureSRTMotionInstanceVulkan
{
    uint32_t type;  // VkAccelerationStructureMotionInstanceTypeNV
    uint32_t flags; // VkAccelerationStructureMotionInstanceFlagsNV
    AccelerationStructureSRTMotionInstanceDescVulkan srtMotionInstance;
    uint8_t padding[8];
};

#ifdef __cplusplus
/// Motion instances should be 160 bytes in size (152 byte payload + 8 byte padding for alignment).
static constexpr size_t kVulkanMotionInstanceSize = 160;

static_assert(
    sizeof(AccelerationStructureStaticMotionInstanceVulkan) == kVulkanMotionInstanceSize,
    "Motion instance structs must match Vulkan stride"
);
static_assert(
    sizeof(AccelerationStructureMatrixMotionInstanceVulkan) == kVulkanMotionInstanceSize,
    "Motion instance structs must match Vulkan stride"
);
static_assert(
    sizeof(AccelerationStructureSRTMotionInstanceVulkan) == kVulkanMotionInstanceSize,
    "Motion instance structs must match Vulkan stride"
);
#endif

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

// ----------------------------------------------------------------------------
// Cluster operations
// ----------------------------------------------------------------------------

static constexpr uint32_t kClusterMaxTriangleCount = 256u;
static constexpr uint32_t kClusterMaxVertexCount = 256u;
static constexpr uint32_t kClusterMaxGeometryIndex = 16777215u;

static constexpr uint32_t kClusterDefaultHandleStride = 8u;
static constexpr uint32_t kClusterOutputAlignment = 128u;

// Cluster flags.
static constexpr uint32_t kClusterFlagNone = 0u;
static constexpr uint32_t kClusterFlagAllowDisableOOMs = (1u << 0);

// Cluster index formats.
static constexpr uint32_t kClusterIndexFormat8bit = 1u;
static constexpr uint32_t kClusterIndexFormat16bit = 2u;
static constexpr uint32_t kClusterIndexFormat32bit = 4u;

// Geometry flags.
static constexpr uint32_t kClusterGeometryFlagNone = 0u;
/// Disables front and back face culling for affected triangles (same behavior as non-cluster geometry).
static constexpr uint32_t kClusterGeometryFlagCullDisable = (1u << 29);
/// Disables any-hit shader invocations for affected triangles (same behavior as non-cluster geometry).
static constexpr uint32_t kClusterGeometryFlagNoDuplicateAnyHitInvocation = (1u << 30);
/// Treats affected triangles as opaque geometry (same behavior as non-cluster geometry).
static constexpr uint32_t kClusterGeometryFlagOpaque = (1u << 31);

/// Arguments for building a triangle cluster.
/// Matches layout of:
/// - D3D12: _NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS
/// - Vulkan: VkClusterAccelerationStructureBuildTriangleClusterInfoNV
/// - OptiX: OptixClusterAccelBuildInputTrianglesIndirectArgs
struct TriangleClusterArgs
{
    /// The user specified identifier to encode in the cluster.
    uint32_t clusterId;
    /// The cluster flags (see kClusterFlagXXX).
    uint32_t clusterFlags;
    /// The number of triangles used by the cluster, up to 256 (kClusterMaxTriangleCount).
    uint32_t triangleCount : 9;
    /// The number of vertices used by the cluster, up to 256 (kClusterMaxVertexCount).
    uint32_t vertexCount : 9;
    /// The number of bits to truncate from the position values.
    uint32_t positionTruncateBitCount : 6;
    /// The index format to use for the indexBuffer (see kClusterIndexFormatXXX).
    uint32_t indexFormat : 4;
    /// The index format to use for the opacityMicromapIndexBuffer (see kClusterIndexFormatXXX).
    uint32_t opacityMicromapIndexFormat : 4;
    /// The base geometry index (lower 24 bit) and base geometry flags (upper 8 bit, see kClusterGeometryFlagXXX).
    /// For OptiX, this represents the SBT index (sbtIndex).
    uint32_t baseGeometryIndexAndFlags;
    /// The stride of the elements of indexBuffer, in bytes. If set to 0, will use index size as stride.
    uint16_t indexBufferStride;
    /// The stride of the elements of vertexBuffer, in bytes. If set to 0, will use vertex size as stride.
    uint16_t vertexBufferStride;
    /// The stride of the elements of geometryIndexBuffer, in bytes. If set to 0, will use 4 byte size as stride.
    uint16_t geometryIndexAndFlagsBufferStride;
    /// The stride of the elements of opacityMicromapIndexBuffer, in bytes. If set to 0, will use index size as stride.
    uint16_t opacityMicromapIndexBufferStride;
    /// The index buffer to construct the cluster.
    DeviceAddress indexBuffer;
    /// The vertex buffer to construct the cluster.
    DeviceAddress vertexBuffer;
    /// (optional) Address of an array of 32-bit geometry indices and geometry flags with size equal to the triangle
    /// count. Each 32-bit value is organized the same as baseGeometryIndexAndFlags. If non-zero, the geometry indices
    /// of the cluster triangles will be equal to the lower 24-bit of geometryIndexBuffer[triangleIndex] +
    /// baseGeometryIndex. If non-zero, the geometry flags for each triangle will be the bitwise OR of the flags in the
    /// upper 8 bits of baseGeometryIndex and geometryIndexBuffer[triangleIndex]. Otherwise all triangles will have a
    /// geometry index equal to baseGeometryIndexAndFlags.
    /// The number of unique elements may not exceed ClusterOperationClasBuildParams::maxUniqueGeometryCount.
    DeviceAddress geometryIndexAndFlagsBuffer;
    /// (optional) Address of a valid OMM array, if used
    /// ClusterOperationFlags::AllowOMM must be set on this and all other cluster operation calls interacting with the
    /// object(s) constructed.
    DeviceAddress opacityMicromapArray;
    /// (optional) Address of an array of indices into the OMM array. Note that an additional OMM special index is
    /// reserved and can be used to turn off OMM for specific triangles.
    DeviceAddress opacityMicromapIndexBuffer;
    /// TODO: Needed for OptiX. D3D12/Vulkan have a different struct adding this field but sharing all other fields.
    DeviceAddress instantiationBoundingBoxLimit;
};

/// Arguments for instantiating cluster from template.
/// Matches layout of:
/// - D3D12: NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_OPERATION_INPUT_TEMPLATES_DESC
/// - Vulkan: VkClusterAccelerationStructureInstantiateClusterInfoNV
/// - OptiX: OptixClusterAccelBuildInputTemplatesArgs
struct InstantiateTemplateArgs
{
    /// The offset added to the clusterId stored in the template to calculate the final clusterId that will be written
    /// to the instantiated cluster.
    uint32_t clusterIdOffset;
    /// The offset added to the geometry index stored for each triangle in the cluster template to calculate the final
    /// geometry index that will be written to the triangles of the instantiated cluster, the resulting value may not
    /// exceed maxGeometryIndexValue both of this call, and the call used to construct the original cluster template
    /// referenced.
    /// For OptiX, this represents the offset added to SBT index (sbtIndexOffset).
    uint32_t geometryIndexOffset;
    /// Address of a previously built cluster template to be instantiated.
    DeviceAddress clusterTemplate;
    /// Vertex buffer with stride to use to fetch the vertex positions used for instantiation.
    /// May be NULL only when used with ClusterOperationMode::GetSizes, which will cause the maximum size for all
    /// possible vertex inputs to be returned.
    DeviceAddress vertexBuffer;
    /// Stride of the vertexBuffer elements, in bytes.
    uint64_t vertexBufferStride;
};

/// Arguments for building acceleration structure from clusters.
/// Matches layout of:
/// - D3D12: NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_INSTANTIATE_TEMPLATE_ARGS
/// - Vulkan: VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV
/// - OptiX: OptixClusterAccelBuildInputClustersArgs
struct ClusterArgs
{
    /// Number of clusters this acceleration structure will be built from.
    uint32_t clusterHandlesCount;
    /// Stride of clusterHandlesBuffer elements, in bytes. Typically 8 (kClusterDefaultHandleStride).
    uint32_t clusterHandlesStride;
    /// Device memory containing the handles of the clusters.
    DeviceAddress clusterHandlesBuffer;
};

} // namespace rhi

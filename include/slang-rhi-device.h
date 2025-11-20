// slang-rhi definitions shared between C++ and Slang.
// This file is included from slang-rhi.h by default.
// Slang shaders can include this file directly.

#ifdef __cplusplus
#include <cstdint>
#endif

namespace rhi {

/// Virtual address in the GPU address space.
typedef uint64_t DeviceAddress;

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
    DeviceAddress AccelerationStructure;
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

namespace cluster {

// -------------------------------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------------------------------

static constexpr uint32_t MAX_CLUSTER_TRIANGLE_COUNT = 256;
static constexpr uint32_t MAX_CLUSTER_VERTEX_COUNT = 256;

static constexpr uint32_t CLUSTER_HANDLE_BYTE_STRIDE = 8;
static constexpr uint32_t CLUSTER_OUTPUT_ALIGNMENT = 128;

// Cluster flags.
static constexpr uint32_t CLUSTER_FLAG_NONE = 0;
static constexpr uint32_t CLUSTER_FLAG_ALLOW_DISABLE_OMMS = (1 << 0);

// Index formats.
static constexpr uint32_t INDEX_FORMAT_8BIT = 1;
static constexpr uint32_t INDEX_FORMAT_16BIT = 2;
static constexpr uint32_t INDEX_FORMAT_32BIT = 4;

// Geometry flags.
static constexpr uint32_t GEOMETRY_FLAG_NONE = 0;
/// Disables front and back face culling for affected triangles (same behavior as non-cluster geometry).
static constexpr uint32_t GEOMETRY_FLAG_CULL_DISABLE = (1 << 29);
/// Disables any-hit shader invocations for affected triangles (same behavior as non-cluster geometry).
static constexpr uint32_t GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION = (1 << 30);
/// Treats affected triangles as opaque geometry (same behavior as non-cluster geometry).
static constexpr uint32_t GEOMETRY_FLAG_OPAQUE = (1 << 31);

/// Arguments for building a triangle cluster.
/// Matches layout of:
/// - D3D12: _NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS
/// - Vulkan: VkClusterAccelerationStructureBuildTriangleClusterInfoNV
/// - OptiX: OptixClusterAccelBuildInputTrianglesIndirectArgs
struct TriangleClusterArgs
{
    /// The user specified identifier to encode in the cluster.
    uint32_t clusterId;
    /// The cluster flags (see ClusterFlags).
    uint32_t clusterFlags;
    /// The number of triangles used by the cluster (max MAX_CLUSTER_TRIANGLE_COUNT).
    uint32_t triangleCount : 9;
    /// The number of vertices used by the cluster. (max MAX_CLUSTER_VERTEX_COUNT).
    uint32_t vertexCount : 9;
    /// The number of bits to truncate from the position values.
    uint32_t positionTruncateBitCount : 6;
    /// The index format to use for the indexBuffer (see IndexFormat).
    uint32_t indexFormat : 4;
    /// The index format to use for the opacityMicromapIndexBuffer (see IndexFormat).
    uint32_t opacityMicromapIndexFormat : 4;
    /// The base geometry index (lower 24 bit) and base geometry flags (upper 8 bit, see GeometryFlags).
    /// For OptiX, this represents the shader binding table index (sbtIndex).
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
    /// TODO: We need portable way for querying this
    /// The number of unique elements may not exceed
    /// NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUT_TRIANGLES_DESC::maxUniqueGeometryCountPerArg.
    DeviceAddress geometryIndexAndFlagsBuffer;
    /// (optional) Address of a valid OMM array, if used
    /// TODO: We need portable flags
    /// NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM must be set on this and all other
    /// cluster operation calls interacting with the object(s) constructed.
    DeviceAddress opacityMicromapArray;
    /// (optional) Address of an array of indices into the OMM array. Note that an additional OMM special index is
    /// reserved and can be used to turn off OMM for specific triangles.
    DeviceAddress opacityMicromapIndexBuffer;
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
    /// For OptiX, this represents the offset added to shader binding table index (sbtIndexOffset).
    uint32_t geometryIndexOffset;
    /// Address of a previously built cluster template to be instantiated.
    DeviceAddress clusterTemplate;
    /// Vertex buffer with stride to use to fetch the vertex positions used for instantiation.
    /// May be NULL only when used with NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_GET_SIZES, which
    /// will cause the maximum size for all possible vertex inputs to be returned. If used, the memory pointed to must
    /// be in state D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE.
    DeviceAddress vertexBuffer; // optional in GET_SIZES (can be 0 to query worst-case size)
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
    /// Stride of clusterHandlesBuffer elements, in bytes. Typically 8.
    uint32_t clusterHandlesStride;
    /// Device memory containing the handles of the clusters.
    DeviceAddress clusterHandlesBuffer;
};

} // namespace cluster

} // namespace rhi

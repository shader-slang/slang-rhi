#include "cpu-acceleration-structure.h"

#include "cpu-buffer.h"
#include "cpu-device.h"

#define NO_CUSTOM_GEOMETRY
#define NO_DOUBLE_PRECISION_SUPPORT
#define NO_THREADED_BUILDS
#define NO_VOXEL_SUPPORT
#define TINYBVH_NO_SIMD
#define TINYBVH_IMPLEMENTATION
#include <tiny_bvh.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace rhi::cpu {

namespace {

struct Float3
{
    float x;
    float y;
    float z;
};

Float3 operator-(const Float3& lhs, const Float3& rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Float3 operator*(const Float3& lhs, float rhs)
{
    return {lhs.x * rhs, lhs.y * rhs, lhs.z * rhs};
}

float dot(const Float3& lhs, const Float3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Float3 cross(const Float3& lhs, const Float3& rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

Float3 transformPoint(const float transform[12], const Float3& point)
{
    return {
        transform[0] * point.x + transform[1] * point.y + transform[2] * point.z + transform[3],
        transform[4] * point.x + transform[5] * point.y + transform[6] * point.z + transform[7],
        transform[8] * point.x + transform[9] * point.y + transform[10] * point.z + transform[11],
    };
}

Float3 transformVector(const float transform[12], const Float3& vector)
{
    return {
        transform[0] * vector.x + transform[1] * vector.y + transform[2] * vector.z,
        transform[4] * vector.x + transform[5] * vector.y + transform[6] * vector.z,
        transform[8] * vector.x + transform[9] * vector.y + transform[10] * vector.z,
    };
}

bool invertAffineTransform(const float transform[12], float inverse[12])
{
    const float a00 = transform[0];
    const float a01 = transform[1];
    const float a02 = transform[2];
    const float a10 = transform[4];
    const float a11 = transform[5];
    const float a12 = transform[6];
    const float a20 = transform[8];
    const float a21 = transform[9];
    const float a22 = transform[10];

    const float determinant =
        a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
    if (!std::isfinite(determinant) || determinant == 0.0f)
        return false;

    const float reciprocalDeterminant = 1.0f / determinant;
    inverse[0] = (a11 * a22 - a12 * a21) * reciprocalDeterminant;
    inverse[1] = (a02 * a21 - a01 * a22) * reciprocalDeterminant;
    inverse[2] = (a01 * a12 - a02 * a11) * reciprocalDeterminant;
    inverse[4] = (a12 * a20 - a10 * a22) * reciprocalDeterminant;
    inverse[5] = (a00 * a22 - a02 * a20) * reciprocalDeterminant;
    inverse[6] = (a02 * a10 - a00 * a12) * reciprocalDeterminant;
    inverse[8] = (a10 * a21 - a11 * a20) * reciprocalDeterminant;
    inverse[9] = (a01 * a20 - a00 * a21) * reciprocalDeterminant;
    inverse[10] = (a00 * a11 - a01 * a10) * reciprocalDeterminant;

    const Float3 translation = {transform[3], transform[7], transform[11]};
    const Float3 inverseTranslation = transformVector(inverse, translation) * -1.0f;
    inverse[3] = inverseTranslation.x;
    inverse[7] = inverseTranslation.y;
    inverse[11] = inverseTranslation.z;
    for (uint32_t elementIndex = 0; elementIndex < 12; ++elementIndex)
    {
        if (!std::isfinite(inverse[elementIndex]))
            return false;
    }
    return true;
}

bool isFinite(const Float3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool hasFlag(AccelerationStructureBuildFlags value, AccelerationStructureBuildFlags flag)
{
    return (uint32_t(value) & uint32_t(flag)) != 0;
}

bool hasFlag(AccelerationStructureGeometryFlags value, AccelerationStructureGeometryFlags flag)
{
    return (uint32_t(value) & uint32_t(flag)) != 0;
}

bool hasFlag(AccelerationStructureInstanceFlags value, AccelerationStructureInstanceFlags flag)
{
    return (uint32_t(value) & uint32_t(flag)) != 0;
}

bool hasRayFlag(uint32_t value, uint32_t flag)
{
    return (value & flag) != 0;
}

// Resolves geometry, instance, and ray overrides in the same order for every BLAS kind.
bool resolveGeometryOpacity(bool geometryOpaque, AccelerationStructureInstanceFlags instanceFlags, uint32_t rayFlags)
{
    bool opaque = geometryOpaque;
    if (hasFlag(instanceFlags, AccelerationStructureInstanceFlags::ForceOpaque))
        opaque = true;
    else if (hasFlag(instanceFlags, AccelerationStructureInstanceFlags::NoOpaque))
        opaque = false;
    if (hasRayFlag(rayFlags, slang_prelude::SLANG_RAY_QUERY_FLAG_FORCE_OPAQUE))
        opaque = true;
    else if (hasRayFlag(rayFlags, slang_prelude::SLANG_RAY_QUERY_FLAG_FORCE_NON_OPAQUE))
        opaque = false;
    return opaque;
}

bool isGeometryCulledByOpacity(bool opaque, uint32_t rayFlags)
{
    return (opaque && hasRayFlag(rayFlags, slang_prelude::SLANG_RAY_QUERY_FLAG_CULL_OPAQUE)) ||
           (!opaque && hasRayFlag(rayFlags, slang_prelude::SLANG_RAY_QUERY_FLAG_CULL_NON_OPAQUE));
}

Result getBufferRange(const BufferOffsetPair& bufferAndOffset, size_t requiredSize, const uint8_t*& outData)
{
    if (!bufferAndOffset.buffer)
        return SLANG_E_INVALID_ARG;

    BufferImpl* buffer = checked_cast<BufferImpl*>(bufferAndOffset.buffer);
    if (bufferAndOffset.offset > buffer->m_desc.size || requiredSize > buffer->m_desc.size - bufferAndOffset.offset)
    {
        return SLANG_E_INVALID_ARG;
    }

    outData = buffer->m_data + bufferAndOffset.offset;
    return SLANG_OK;
}

Result readVertex(
    const AccelerationStructureBuildInputTriangles& triangles,
    uint32_t vertexIndex,
    const uint8_t* vertexData,
    Float3& outVertex
)
{
    if (vertexIndex >= triangles.vertexCount)
        return SLANG_E_INVALID_ARG;

    std::memcpy(&outVertex, vertexData + size_t(vertexIndex) * triangles.vertexStride, sizeof(Float3));
    if (!isFinite(outVertex))
        return SLANG_E_INVALID_ARG;
    return SLANG_OK;
}

bool intersectAABB(
    const Float3& origin,
    const Float3& direction,
    const tinybvh::bvhvec3& boundsMin,
    const tinybvh::bvhvec3& boundsMax,
    float rayTMin,
    float rayTMax,
    float& outEntry
)
{
    float entry = rayTMin;
    float exit = rayTMax;
    const float originValues[3] = {origin.x, origin.y, origin.z};
    const float directionValues[3] = {direction.x, direction.y, direction.z};
    const float minValues[3] = {boundsMin.x, boundsMin.y, boundsMin.z};
    const float maxValues[3] = {boundsMax.x, boundsMax.y, boundsMax.z};

    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (directionValues[axis] == 0.0f)
        {
            if (originValues[axis] < minValues[axis] || originValues[axis] > maxValues[axis])
                return false;
            continue;
        }

        const float reciprocalDirection = 1.0f / directionValues[axis];
        float axisEntry = (minValues[axis] - originValues[axis]) * reciprocalDirection;
        float axisExit = (maxValues[axis] - originValues[axis]) * reciprocalDirection;
        if (axisEntry > axisExit)
        {
            const float temporary = axisEntry;
            axisEntry = axisExit;
            axisExit = temporary;
        }
        entry = rhi::max(entry, axisEntry);
        exit = rhi::min(exit, axisExit);
        if (exit < entry)
            return false;
    }

    outEntry = entry;
    return true;
}

bool intersectTriangle(
    const Float3& rayOrigin,
    const Float3& rayDirection,
    const Float3& vertex0,
    const Float3& vertex1,
    const Float3& vertex2,
    float rayTMin,
    float rayTMax,
    float& outRayT,
    float& outBarycentricU,
    float& outBarycentricV,
    float& outObjectFacing
)
{
    const Float3 edge1 = vertex1 - vertex0;
    const Float3 edge2 = vertex2 - vertex0;
    const Float3 p = cross(rayDirection, edge2);
    const float determinant = dot(edge1, p);
    if (!std::isfinite(determinant) || determinant == 0.0f)
        return false;

    const float inverseDeterminant = 1.0f / determinant;
    const Float3 vertex0ToOrigin = rayOrigin - vertex0;
    const float u = dot(vertex0ToOrigin, p) * inverseDeterminant;
    if (!std::isfinite(u) || u < 0.0f || u > 1.0f)
        return false;

    const Float3 q = cross(vertex0ToOrigin, edge1);
    const float v = dot(rayDirection, q) * inverseDeterminant;
    if (!std::isfinite(v) || v < 0.0f || u + v > 1.0f)
        return false;

    const float rayT = dot(edge2, q) * inverseDeterminant;
    if (!std::isfinite(rayT) || rayT <= rayTMin || rayT >= rayTMax)
        return false;

    outRayT = rayT;
    outBarycentricU = u;
    outBarycentricV = v;
    outObjectFacing = dot(cross(edge1, edge2), rayDirection);
    return true;
}

void completeTraversal(slang_prelude::RayQueryState* state)
{
    state->candidatePending = 0;
    state->traversalPhase = slang_prelude::SLANG_RAY_QUERY_TRAVERSAL_COMPLETE;
    state->tlasNode = slang_prelude::RayQueryState::kInvalidNode;
    state->blasNode = slang_prelude::RayQueryState::kInvalidNode;
    state->tlasStackSize = 0;
    state->blasStackSize = 0;
}

} // namespace

struct AccelerationStructureImpl::Impl
{
    enum class Kind
    {
        Unbuilt,
        BottomLevel,
        TopLevel,
    };

    enum class GeometryKind
    {
        None,
        Triangles,
        ProceduralPrimitives,
    };

    struct Triangle
    {
        uint32_t geometryIndex;
        uint32_t primitiveIndex;
        bool opaque;
    };

    struct ProceduralPrimitive
    {
        uint32_t geometryIndex;
        uint32_t primitiveIndex;
        bool opaque;
    };

    struct Instance
    {
        RefPtr<AccelerationStructureImpl> bottomLevel;
        float objectToWorld[12];
        float worldToObject[12];
        uint32_t instanceID;
        uint32_t instanceMask;
        uint32_t instanceContributionToHitGroupIndex;
        AccelerationStructureInstanceFlags flags;
    };

    Kind kind = Kind::Unbuilt;
    GeometryKind geometryKind = GeometryKind::None;
    tinybvh::BVH bvh;
    std::vector<tinybvh::bvhvec4> vertices;
    std::vector<Triangle> triangles;
    std::vector<ProceduralPrimitive> proceduralPrimitives;
    std::vector<tinybvh::bvhvec4> proceduralBounds;
    std::vector<Instance> instances;
    std::vector<tinybvh::bvhvec4> instanceBounds;
};

AccelerationStructureImpl::AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc)
    : AccelerationStructure(device, desc)
    , m_impl(new Impl())
{
}

AccelerationStructureImpl::~AccelerationStructureImpl() = default;

Result AccelerationStructureImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

AccelerationStructureHandle AccelerationStructureImpl::getHandle()
{
    return AccelerationStructureHandle{uint64_t(reinterpret_cast<uintptr_t>(this))};
}

DeviceAddress AccelerationStructureImpl::getDeviceAddress()
{
    return DeviceAddress(reinterpret_cast<uintptr_t>(this));
}

Result AccelerationStructureImpl::getDescriptorHandle(DescriptorHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result AccelerationStructureImpl::build(const AccelerationStructureBuildDesc& desc)
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    if (!desc.inputs || desc.inputCount == 0)
    {
        device->printError("CPU acceleration-structure builds require at least one input.\n");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.mode != AccelerationStructureBuildMode::Build)
    {
        device->printError("CPU acceleration structures do not support update builds.\n");
        return SLANG_E_NOT_AVAILABLE;
    }
    if (desc.motionOptions.keyCount != 1 || hasFlag(desc.flags, AccelerationStructureBuildFlags::CreateMotion))
    {
        device->printError("CPU acceleration structures do not support motion builds.\n");
        return SLANG_E_NOT_AVAILABLE;
    }
    if (hasFlag(desc.flags, AccelerationStructureBuildFlags::AllowUpdate))
    {
        device->printError("CPU acceleration structures do not support update flags.\n");
        return SLANG_E_NOT_AVAILABLE;
    }

    const AccelerationStructureBuildInputType inputType = desc.inputs[0].type;
    for (uint32_t inputIndex = 1; inputIndex < desc.inputCount; ++inputIndex)
    {
        if (desc.inputs[inputIndex].type != inputType)
        {
            device->printError("CPU acceleration-structure build inputs must all have the same type.\n");
            return SLANG_E_INVALID_ARG;
        }
    }

    std::shared_ptr<Impl> newImpl(new Impl());
    if (inputType == AccelerationStructureBuildInputType::Triangles)
    {
        if (m_desc.kind == AccelerationStructureKind::TopLevel)
        {
            device->printError("Triangle inputs cannot build a top-level CPU acceleration structure.\n");
            return SLANG_E_INVALID_ARG;
        }

        for (uint32_t geometryIndex = 0; geometryIndex < desc.inputCount; ++geometryIndex)
        {
            const AccelerationStructureBuildInputTriangles& triangles = desc.inputs[geometryIndex].triangles;
            if (triangles.vertexBufferCount != 1 ||
                (triangles.vertexFormat != Format::RGB32Float && triangles.vertexFormat != Format::RGBA32Float))
            {
                device->printError("CPU triangle builds require one RGB32Float or RGBA32Float vertex buffer.\n");
                return SLANG_E_NOT_AVAILABLE;
            }

            const uint32_t minimumVertexStride = triangles.vertexFormat == Format::RGB32Float ? 12u : 16u;
            if (triangles.vertexStride < minimumVertexStride || triangles.vertexCount == 0)
            {
                device->printError("CPU triangle builds received an invalid vertex stride or count.\n");
                return SLANG_E_INVALID_ARG;
            }

            const uint8_t* vertexData = nullptr;
            const size_t vertexDataSize =
                size_t(triangles.vertexCount - 1) * triangles.vertexStride + minimumVertexStride;
            SLANG_RETURN_ON_FAIL(getBufferRange(triangles.vertexBuffers[0], vertexDataSize, vertexData));

            float preTransform[12] = {};
            bool hasPreTransform = bool(triangles.preTransformBuffer);
            if (hasPreTransform)
            {
                const uint8_t* preTransformData = nullptr;
                SLANG_RETURN_ON_FAIL(
                    getBufferRange(triangles.preTransformBuffer, sizeof(preTransform), preTransformData)
                );
                std::memcpy(preTransform, preTransformData, sizeof(preTransform));
                for (float value : preTransform)
                {
                    if (!std::isfinite(value))
                    {
                        device->printError("CPU triangle pre-transforms must contain finite values.\n");
                        return SLANG_E_INVALID_ARG;
                    }
                }
            }

            const bool indexed = bool(triangles.indexBuffer);
            uint32_t primitiveCount = 0;
            const uint8_t* indexData = nullptr;
            uint32_t indexSize = 0;
            if (indexed)
            {
                if (triangles.indexCount == 0 || triangles.indexCount % 3 != 0)
                {
                    device->printError("CPU indexed triangle builds require complete triangles.\n");
                    return SLANG_E_INVALID_ARG;
                }
                if (triangles.indexFormat == IndexFormat::Uint16)
                    indexSize = 2;
                else if (triangles.indexFormat == IndexFormat::Uint32)
                    indexSize = 4;
                else
                    return SLANG_E_NOT_AVAILABLE;

                SLANG_RETURN_ON_FAIL(
                    getBufferRange(triangles.indexBuffer, size_t(triangles.indexCount) * indexSize, indexData)
                );
                primitiveCount = triangles.indexCount / 3;
            }
            else
            {
                if (triangles.vertexCount % 3 != 0)
                {
                    device->printError("CPU non-indexed triangle builds require complete triangles.\n");
                    return SLANG_E_INVALID_ARG;
                }
                primitiveCount = triangles.vertexCount / 3;
            }

            const bool opaque = hasFlag(triangles.flags, AccelerationStructureGeometryFlags::Opaque);
            for (uint32_t primitiveIndex = 0; primitiveIndex < primitiveCount; ++primitiveIndex)
            {
                uint32_t vertexIndices[3] = {};
                for (uint32_t corner = 0; corner < 3; ++corner)
                {
                    const uint32_t elementIndex = primitiveIndex * 3 + corner;
                    if (indexed)
                    {
                        if (indexSize == 2)
                        {
                            uint16_t index = 0;
                            std::memcpy(&index, indexData + size_t(elementIndex) * indexSize, indexSize);
                            vertexIndices[corner] = index;
                        }
                        else
                        {
                            std::memcpy(
                                &vertexIndices[corner],
                                indexData + size_t(elementIndex) * indexSize,
                                indexSize
                            );
                        }
                    }
                    else
                    {
                        vertexIndices[corner] = elementIndex;
                    }

                    Float3 vertex = {};
                    SLANG_RETURN_ON_FAIL(readVertex(triangles, vertexIndices[corner], vertexData, vertex));
                    if (hasPreTransform)
                        vertex = transformPoint(preTransform, vertex);
                    if (!isFinite(vertex))
                    {
                        device->printError("CPU triangle builds require finite transformed vertices.\n");
                        return SLANG_E_INVALID_ARG;
                    }
                    newImpl->vertices.emplace_back(vertex.x, vertex.y, vertex.z, 0.0f);
                }
                newImpl->triangles.push_back({geometryIndex, primitiveIndex, opaque});
            }
        }

        if (newImpl->triangles.empty())
        {
            device->printError("CPU bottom-level acceleration structures cannot be empty.\n");
            return SLANG_E_INVALID_ARG;
        }

        newImpl->bvh.Build(newImpl->vertices.data(), uint32_t(newImpl->triangles.size()));
        newImpl->kind = Impl::Kind::BottomLevel;
        newImpl->geometryKind = Impl::GeometryKind::Triangles;
        m_desc.kind = AccelerationStructureKind::BottomLevel;
    }
    else if (inputType == AccelerationStructureBuildInputType::ProceduralPrimitives)
    {
        if (m_desc.kind == AccelerationStructureKind::TopLevel)
        {
            device->printError("Procedural inputs cannot build a top-level CPU acceleration structure.\n");
            return SLANG_E_INVALID_ARG;
        }

        for (uint32_t geometryIndex = 0; geometryIndex < desc.inputCount; ++geometryIndex)
        {
            const AccelerationStructureBuildInputProceduralPrimitives& procedural =
                desc.inputs[geometryIndex].proceduralPrimitives;
            if (procedural.aabbBufferCount != 1)
            {
                device->printError("CPU procedural builds require exactly one AABB buffer.\n");
                return SLANG_E_NOT_AVAILABLE;
            }
            if (procedural.aabbStride < sizeof(AccelerationStructureAABB) || procedural.primitiveCount == 0)
            {
                device->printError("CPU procedural builds received an invalid AABB stride or primitive count.\n");
                return SLANG_E_INVALID_ARG;
            }

            const uint8_t* aabbData = nullptr;
            const size_t aabbDataSize =
                size_t(procedural.primitiveCount - 1) * procedural.aabbStride + sizeof(AccelerationStructureAABB);
            SLANG_RETURN_ON_FAIL(getBufferRange(procedural.aabbBuffers[0], aabbDataSize, aabbData));

            const bool opaque = hasFlag(procedural.flags, AccelerationStructureGeometryFlags::Opaque);
            for (uint32_t primitiveIndex = 0; primitiveIndex < procedural.primitiveCount; ++primitiveIndex)
            {
                AccelerationStructureAABB aabb = {};
                std::memcpy(&aabb, aabbData + size_t(primitiveIndex) * procedural.aabbStride, sizeof(aabb));
                const Float3 boundsMin = {aabb.minX, aabb.minY, aabb.minZ};
                const Float3 boundsMax = {aabb.maxX, aabb.maxY, aabb.maxZ};
                if (!isFinite(boundsMin) || !isFinite(boundsMax) || boundsMin.x > boundsMax.x ||
                    boundsMin.y > boundsMax.y || boundsMin.z > boundsMax.z)
                {
                    device->printError("CPU procedural builds require finite, ordered AABB bounds.\n");
                    return SLANG_E_INVALID_ARG;
                }

                newImpl->proceduralPrimitives.push_back({geometryIndex, primitiveIndex, opaque});
                newImpl->proceduralBounds.emplace_back(boundsMin.x, boundsMin.y, boundsMin.z, 0.0f);
                newImpl->proceduralBounds.emplace_back(boundsMax.x, boundsMax.y, boundsMax.z, 0.0f);
            }
        }

        if (newImpl->proceduralPrimitives.empty())
        {
            device->printError("CPU procedural bottom-level acceleration structures cannot be empty.\n");
            return SLANG_E_INVALID_ARG;
        }

        newImpl->bvh.BuildAABB(newImpl->proceduralBounds.data(), uint32_t(newImpl->proceduralPrimitives.size()));
        newImpl->kind = Impl::Kind::BottomLevel;
        newImpl->geometryKind = Impl::GeometryKind::ProceduralPrimitives;
        m_desc.kind = AccelerationStructureKind::BottomLevel;
    }
    else if (inputType == AccelerationStructureBuildInputType::Instances)
    {
        if (m_desc.kind == AccelerationStructureKind::BottomLevel)
        {
            device->printError("Instance inputs cannot build a bottom-level CPU acceleration structure.\n");
            return SLANG_E_INVALID_ARG;
        }

        for (uint32_t inputIndex = 0; inputIndex < desc.inputCount; ++inputIndex)
        {
            const AccelerationStructureBuildInputInstances& instances = desc.inputs[inputIndex].instances;
            const uint32_t instanceStride = instances.instanceStride
                                                ? instances.instanceStride
                                                : uint32_t(sizeof(AccelerationStructureInstanceDescGeneric));
            if (instanceStride < sizeof(AccelerationStructureInstanceDescGeneric))
            {
                device->printError("CPU instance descriptors have an invalid stride.\n");
                return SLANG_E_INVALID_ARG;
            }
            if (instances.instanceCount == 0)
                continue;

            const uint8_t* instanceData = nullptr;
            const size_t instanceDataSize =
                size_t(instances.instanceCount - 1) * instanceStride + sizeof(AccelerationStructureInstanceDescGeneric);
            SLANG_RETURN_ON_FAIL(getBufferRange(instances.instanceBuffer, instanceDataSize, instanceData));

            for (uint32_t localInstanceIndex = 0; localInstanceIndex < instances.instanceCount; ++localInstanceIndex)
            {
                AccelerationStructureInstanceDescGeneric instanceDesc = {};
                std::memcpy(
                    &instanceDesc,
                    instanceData + size_t(localInstanceIndex) * instanceStride,
                    sizeof(instanceDesc)
                );

                AccelerationStructureImpl* bottomLevel =
                    reinterpret_cast<AccelerationStructureImpl*>(uintptr_t(instanceDesc.accelerationStructure.value));
                if (!bottomLevel || !bottomLevel->m_impl || bottomLevel->m_impl->kind != Impl::Kind::BottomLevel)
                {
                    device->printError(
                        "CPU top-level acceleration structures require CPU bottom-level instance handles.\n"
                    );
                    return SLANG_E_INVALID_ARG;
                }

                Impl::Instance instance = {};
                instance.bottomLevel = RefPtr<AccelerationStructureImpl>(bottomLevel);
                std::memcpy(instance.objectToWorld, instanceDesc.transform, sizeof(instance.objectToWorld));
                for (float value : instance.objectToWorld)
                {
                    if (!std::isfinite(value))
                    {
                        device->printError("CPU instance transforms must contain finite values.\n");
                        return SLANG_E_INVALID_ARG;
                    }
                }
                if (!invertAffineTransform(instance.objectToWorld, instance.worldToObject))
                {
                    device->printError("CPU instance transforms must be invertible.\n");
                    return SLANG_E_INVALID_ARG;
                }

                instance.instanceID = instanceDesc.instanceID;
                instance.instanceMask = instanceDesc.instanceMask;
                instance.instanceContributionToHitGroupIndex = instanceDesc.instanceContributionToHitGroupIndex;
                instance.flags = instanceDesc.flags;

                const tinybvh::bvhvec3 objectBoundsMin = bottomLevel->m_impl->bvh.bvhNode[0].aabbMin;
                const tinybvh::bvhvec3 objectBoundsMax = bottomLevel->m_impl->bvh.bvhNode[0].aabbMax;
                Float3 worldBoundsMin = {
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity(),
                };
                Float3 worldBoundsMax = {
                    -std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),
                };
                for (uint32_t corner = 0; corner < 8; ++corner)
                {
                    const Float3 objectCorner = {
                        (corner & 1) ? objectBoundsMax.x : objectBoundsMin.x,
                        (corner & 2) ? objectBoundsMax.y : objectBoundsMin.y,
                        (corner & 4) ? objectBoundsMax.z : objectBoundsMin.z,
                    };
                    const Float3 worldCorner = transformPoint(instance.objectToWorld, objectCorner);
                    if (!isFinite(worldCorner))
                    {
                        device->printError("CPU instance transforms produce non-finite bounds.\n");
                        return SLANG_E_INVALID_ARG;
                    }
                    worldBoundsMin.x = rhi::min(worldBoundsMin.x, worldCorner.x);
                    worldBoundsMin.y = rhi::min(worldBoundsMin.y, worldCorner.y);
                    worldBoundsMin.z = rhi::min(worldBoundsMin.z, worldCorner.z);
                    worldBoundsMax.x = rhi::max(worldBoundsMax.x, worldCorner.x);
                    worldBoundsMax.y = rhi::max(worldBoundsMax.y, worldCorner.y);
                    worldBoundsMax.z = rhi::max(worldBoundsMax.z, worldCorner.z);
                }

                newImpl->instances.push_back(std::move(instance));
                newImpl->instanceBounds.emplace_back(worldBoundsMin.x, worldBoundsMin.y, worldBoundsMin.z, 0.0f);
                newImpl->instanceBounds.emplace_back(worldBoundsMax.x, worldBoundsMax.y, worldBoundsMax.z, 0.0f);
            }
        }

        if (newImpl->instances.empty())
        {
            device->printError("CPU top-level acceleration structures cannot be empty.\n");
            return SLANG_E_INVALID_ARG;
        }

        newImpl->bvh.BuildAABB(newImpl->instanceBounds.data(), uint32_t(newImpl->instances.size()));
        newImpl->kind = Impl::Kind::TopLevel;
        m_desc.kind = AccelerationStructureKind::TopLevel;
    }
    else
    {
        device->printError(
            "CPU acceleration structures support triangles, procedural primitives, and instances only; "
            "spheres and linear swept spheres are unavailable.\n"
        );
        return SLANG_E_NOT_AVAILABLE;
    }

    m_impl = std::move(newImpl);
    return SLANG_OK;
}

Result AccelerationStructureImpl::copyFrom(const AccelerationStructureImpl& source)
{
    if (!source.m_impl || source.m_impl->kind == Impl::Kind::Unbuilt)
        return SLANG_E_INVALID_ARG;
    if (m_desc.size < source.m_desc.size)
        return SLANG_E_INVALID_ARG;

    // CPU acceleration structures keep their hierarchy in backend-owned memory rather than in
    // the logical allocation described by AccelerationStructureDesc::size. A compact copy
    // therefore shares the immutable built hierarchy; a later rebuild replaces only the rebuilt
    // object's reference and does not mutate existing copies.
    m_impl = source.m_impl;
    m_desc.kind = source.m_desc.kind;
    return SLANG_OK;
}

bool AccelerationStructureImpl::proceed(slang_prelude::RayQueryState* state) const
{
    using namespace slang_prelude;

    // TinyBVH's one-shot Intersect() cannot expose the non-opaque candidate suspension points
    // required by RayQuery. Traverse its public BVH nodes directly and keep every mutable cursor
    // in the shader-owned state so the built TLAS and BLAS objects remain read-only and shareable.
    if (!state)
    {
        SLANG_RHI_ASSERT_FAILURE("CPU RayQuery requires a valid traversal state");
        return false;
    }
    if (!m_impl || m_impl->kind != Impl::Kind::TopLevel)
    {
        SLANG_RHI_ASSERT_FAILURE("CPU RayQuery requires a built top-level acceleration structure");
        completeTraversal(state);
        return false;
    }

    const Float3 worldRayOrigin = {
        state->worldRayOrigin[0],
        state->worldRayOrigin[1],
        state->worldRayOrigin[2],
    };
    const Float3 worldRayDirection = {
        state->worldRayDirection[0],
        state->worldRayDirection[1],
        state->worldRayDirection[2],
    };

    while (state->traversalPhase != SLANG_RAY_QUERY_TRAVERSAL_COMPLETE)
    {
        const float rayTMax =
            state->committedStatus != SLANG_RAY_QUERY_COMMITTED_NOTHING ? state->committed.rayT : state->rayTMax;

        if (state->traversalPhase == SLANG_RAY_QUERY_TRAVERSAL_TLAS)
        {
            if (state->tlasNode == RayQueryState::kInvalidNode)
            {
                if (state->tlasStackSize == 0)
                {
                    completeTraversal(state);
                    return false;
                }
                state->tlasNode = state->tlasStack[--state->tlasStackSize];
                state->tlasLeafOffset = 0;
            }

            const tinybvh::BVH::BVHNode& node = m_impl->bvh.bvhNode[state->tlasNode];
            if (!node.isLeaf())
            {
                const uint32_t leftIndex = node.leftFirst;
                const uint32_t rightIndex = node.leftFirst + 1;
                const tinybvh::BVH::BVHNode& left = m_impl->bvh.bvhNode[leftIndex];
                const tinybvh::BVH::BVHNode& right = m_impl->bvh.bvhNode[rightIndex];
                float leftEntry = 0.0f;
                float rightEntry = 0.0f;
                const bool hitLeft = intersectAABB(
                    worldRayOrigin,
                    worldRayDirection,
                    left.aabbMin,
                    left.aabbMax,
                    state->rayTMin,
                    rayTMax,
                    leftEntry
                );
                const bool hitRight = intersectAABB(
                    worldRayOrigin,
                    worldRayDirection,
                    right.aabbMin,
                    right.aabbMax,
                    state->rayTMin,
                    rayTMax,
                    rightEntry
                );

                state->tlasNode = RayQueryState::kInvalidNode;
                if (hitLeft && hitRight)
                {
                    if (state->tlasStackSize >= RayQueryState::kTLASStackCapacity)
                    {
                        SLANG_RHI_ASSERT_FAILURE("CPU RayQuery TLAS traversal stack overflow");
                        completeTraversal(state);
                        return false;
                    }
                    if (leftEntry <= rightEntry)
                    {
                        state->tlasNode = leftIndex;
                        state->tlasStack[state->tlasStackSize++] = rightIndex;
                    }
                    else
                    {
                        state->tlasNode = rightIndex;
                        state->tlasStack[state->tlasStackSize++] = leftIndex;
                    }
                }
                else if (hitLeft)
                {
                    state->tlasNode = leftIndex;
                }
                else if (hitRight)
                {
                    state->tlasNode = rightIndex;
                }
                continue;
            }

            bool enteredBottomLevel = false;
            while (state->tlasLeafOffset < node.triCount)
            {
                const uint32_t instanceIndex = m_impl->bvh.primIdx[node.leftFirst + state->tlasLeafOffset++];
                const Impl::Instance& instance = m_impl->instances[instanceIndex];
                if ((instance.instanceMask & state->instanceInclusionMask) == 0)
                    continue;
                const Impl::GeometryKind geometryKind = instance.bottomLevel->m_impl->geometryKind;
                if ((geometryKind == Impl::GeometryKind::Triangles &&
                     hasRayFlag(state->rayFlags, SLANG_RAY_QUERY_FLAG_SKIP_TRIANGLES)) ||
                    (geometryKind == Impl::GeometryKind::ProceduralPrimitives &&
                     hasRayFlag(state->rayFlags, SLANG_RAY_QUERY_FLAG_SKIP_PROCEDURAL_PRIMITIVES)))
                {
                    continue;
                }

                state->currentInstanceIndex = instanceIndex;
                state->blasNode = 0;
                state->blasLeafOffset = 0;
                state->blasStackSize = 0;
                state->traversalPhase = SLANG_RAY_QUERY_TRAVERSAL_BLAS;
                enteredBottomLevel = true;
                break;
            }
            if (!enteredBottomLevel)
            {
                state->tlasNode = RayQueryState::kInvalidNode;
                state->tlasLeafOffset = 0;
            }
            continue;
        }

        const Impl::Instance& instance = m_impl->instances[state->currentInstanceIndex];
        const Impl& bottomLevel = *instance.bottomLevel->m_impl;
        const Float3 objectRayOrigin = transformPoint(instance.worldToObject, worldRayOrigin);
        const Float3 objectRayDirection = transformVector(instance.worldToObject, worldRayDirection);

        if (state->blasNode == RayQueryState::kInvalidNode)
        {
            if (state->blasStackSize == 0)
            {
                state->traversalPhase = SLANG_RAY_QUERY_TRAVERSAL_TLAS;
                continue;
            }
            state->blasNode = state->blasStack[--state->blasStackSize];
            state->blasLeafOffset = 0;
        }

        const tinybvh::BVH::BVHNode& node = bottomLevel.bvh.bvhNode[state->blasNode];
        if (!node.isLeaf())
        {
            const uint32_t leftIndex = node.leftFirst;
            const uint32_t rightIndex = node.leftFirst + 1;
            const tinybvh::BVH::BVHNode& left = bottomLevel.bvh.bvhNode[leftIndex];
            const tinybvh::BVH::BVHNode& right = bottomLevel.bvh.bvhNode[rightIndex];
            float leftEntry = 0.0f;
            float rightEntry = 0.0f;
            const bool hitLeft = intersectAABB(
                objectRayOrigin,
                objectRayDirection,
                left.aabbMin,
                left.aabbMax,
                state->rayTMin,
                rayTMax,
                leftEntry
            );
            const bool hitRight = intersectAABB(
                objectRayOrigin,
                objectRayDirection,
                right.aabbMin,
                right.aabbMax,
                state->rayTMin,
                rayTMax,
                rightEntry
            );

            state->blasNode = RayQueryState::kInvalidNode;
            if (hitLeft && hitRight)
            {
                if (state->blasStackSize >= RayQueryState::kBLASStackCapacity)
                {
                    SLANG_RHI_ASSERT_FAILURE("CPU RayQuery BLAS traversal stack overflow");
                    completeTraversal(state);
                    return false;
                }
                if (leftEntry <= rightEntry)
                {
                    state->blasNode = leftIndex;
                    state->blasStack[state->blasStackSize++] = rightIndex;
                }
                else
                {
                    state->blasNode = rightIndex;
                    state->blasStack[state->blasStackSize++] = leftIndex;
                }
            }
            else if (hitLeft)
            {
                state->blasNode = leftIndex;
            }
            else if (hitRight)
            {
                state->blasNode = rightIndex;
            }
            continue;
        }

        while (state->blasLeafOffset < node.triCount)
        {
            const uint32_t primitiveIndex = bottomLevel.bvh.primIdx[node.leftFirst + state->blasLeafOffset++];

            if (bottomLevel.geometryKind == Impl::GeometryKind::ProceduralPrimitives)
            {
                const Impl::ProceduralPrimitive& primitive = bottomLevel.proceduralPrimitives[primitiveIndex];
                const bool opaque = resolveGeometryOpacity(primitive.opaque, instance.flags, state->rayFlags);
                if (isGeometryCulledByOpacity(opaque, state->rayFlags))
                    continue;

                const tinybvh::bvhvec4& boundsMin = bottomLevel.proceduralBounds[primitiveIndex * 2];
                const tinybvh::bvhvec4& boundsMax = bottomLevel.proceduralBounds[primitiveIndex * 2 + 1];
                const tinybvh::bvhvec3 boundsMin3(boundsMin.x, boundsMin.y, boundsMin.z);
                const tinybvh::bvhvec3 boundsMax3(boundsMax.x, boundsMax.y, boundsMax.z);
                float boundsEntry = 0.0f;
                const float currentRayTMax = state->committedStatus != SLANG_RAY_QUERY_COMMITTED_NOTHING
                                                 ? state->committed.rayT
                                                 : state->rayTMax;
                if (!intersectAABB(
                        objectRayOrigin,
                        objectRayDirection,
                        boundsMin3,
                        boundsMax3,
                        state->rayTMin,
                        currentRayTMax,
                        boundsEntry
                    ))
                {
                    continue;
                }

                RayQueryHit hit = {};
                hit.rayT = boundsEntry;
                hit.objectRayOrigin[0] = objectRayOrigin.x;
                hit.objectRayOrigin[1] = objectRayOrigin.y;
                hit.objectRayOrigin[2] = objectRayOrigin.z;
                hit.objectRayDirection[0] = objectRayDirection.x;
                hit.objectRayDirection[1] = objectRayDirection.y;
                hit.objectRayDirection[2] = objectRayDirection.z;
                std::memcpy(hit.objectToWorld, instance.objectToWorld, sizeof(hit.objectToWorld));
                std::memcpy(hit.worldToObject, instance.worldToObject, sizeof(hit.worldToObject));
                hit.instanceIndex = state->currentInstanceIndex;
                hit.instanceID = instance.instanceID;
                hit.instanceContributionToHitGroupIndex = instance.instanceContributionToHitGroupIndex;
                hit.geometryIndex = primitive.geometryIndex;
                hit.primitiveIndex = primitive.primitiveIndex;
                hit.proceduralPrimitiveNonOpaque = opaque ? 0u : 1u;

                state->candidate = hit;
                state->candidateType = SLANG_RAY_QUERY_CANDIDATE_PROCEDURAL_PRIMITIVE;
                state->candidatePending = 1;
                return true;
            }

            SLANG_RHI_ASSERT(bottomLevel.geometryKind == Impl::GeometryKind::Triangles);
            const uint32_t triangleIndex = primitiveIndex;
            const Impl::Triangle& triangle = bottomLevel.triangles[triangleIndex];

            const bool opaque = resolveGeometryOpacity(triangle.opaque, instance.flags, state->rayFlags);
            if (isGeometryCulledByOpacity(opaque, state->rayFlags))
                continue;

            const tinybvh::bvhvec4& tinyVertex0 = bottomLevel.vertices[triangleIndex * 3];
            const tinybvh::bvhvec4& tinyVertex1 = bottomLevel.vertices[triangleIndex * 3 + 1];
            const tinybvh::bvhvec4& tinyVertex2 = bottomLevel.vertices[triangleIndex * 3 + 2];
            const Float3 vertex0 = {tinyVertex0.x, tinyVertex0.y, tinyVertex0.z};
            const Float3 vertex1 = {tinyVertex1.x, tinyVertex1.y, tinyVertex1.z};
            const Float3 vertex2 = {tinyVertex2.x, tinyVertex2.y, tinyVertex2.z};

            float rayT = 0.0f;
            float barycentricU = 0.0f;
            float barycentricV = 0.0f;
            float objectFacing = 0.0f;
            const float currentRayTMax =
                state->committedStatus != SLANG_RAY_QUERY_COMMITTED_NOTHING ? state->committed.rayT : state->rayTMax;
            if (!intersectTriangle(
                    objectRayOrigin,
                    objectRayDirection,
                    vertex0,
                    vertex1,
                    vertex2,
                    state->rayTMin,
                    currentRayTMax,
                    rayT,
                    barycentricU,
                    barycentricV,
                    objectFacing
                ))
            {
                continue;
            }

            // DXR defines instance winding in BLAS object space. The instance transform must not
            // apply an additional determinant flip; a geometry pre-transform is already baked
            // into these object-space vertices and therefore naturally affects their winding.
            const bool counterClockwiseFront = objectFacing > 0.0f;
            const bool frontFace =
                hasFlag(instance.flags, AccelerationStructureInstanceFlags::TriangleFrontCounterClockwise)
                    ? counterClockwiseFront
                    : !counterClockwiseFront;
            if (!hasFlag(instance.flags, AccelerationStructureInstanceFlags::TriangleFacingCullDisable))
            {
                if ((frontFace && hasRayFlag(state->rayFlags, SLANG_RAY_QUERY_FLAG_CULL_FRONT_FACING_TRIANGLES)) ||
                    (!frontFace && hasRayFlag(state->rayFlags, SLANG_RAY_QUERY_FLAG_CULL_BACK_FACING_TRIANGLES)))
                {
                    continue;
                }
            }

            RayQueryHit hit = {};
            hit.rayT = rayT;
            hit.barycentrics[0] = barycentricU;
            hit.barycentrics[1] = barycentricV;
            hit.objectRayOrigin[0] = objectRayOrigin.x;
            hit.objectRayOrigin[1] = objectRayOrigin.y;
            hit.objectRayOrigin[2] = objectRayOrigin.z;
            hit.objectRayDirection[0] = objectRayDirection.x;
            hit.objectRayDirection[1] = objectRayDirection.y;
            hit.objectRayDirection[2] = objectRayDirection.z;
            std::memcpy(hit.objectToWorld, instance.objectToWorld, sizeof(hit.objectToWorld));
            std::memcpy(hit.worldToObject, instance.worldToObject, sizeof(hit.worldToObject));
            hit.instanceIndex = state->currentInstanceIndex;
            hit.instanceID = instance.instanceID;
            hit.instanceContributionToHitGroupIndex = instance.instanceContributionToHitGroupIndex;
            hit.geometryIndex = triangle.geometryIndex;
            hit.primitiveIndex = triangle.primitiveIndex;
            hit.triangleFrontFace = frontFace ? 1u : 0u;

            if (opaque)
            {
                if (state->committedStatus == SLANG_RAY_QUERY_COMMITTED_NOTHING || hit.rayT < state->committed.rayT)
                {
                    state->committed = hit;
                    state->committedStatus = SLANG_RAY_QUERY_COMMITTED_TRIANGLE_HIT;
                }
                if (hasRayFlag(state->rayFlags, SLANG_RAY_QUERY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH))
                {
                    completeTraversal(state);
                    return false;
                }
                continue;
            }

            state->candidate = hit;
            state->candidateType = SLANG_RAY_QUERY_CANDIDATE_NON_OPAQUE_TRIANGLE;
            state->candidatePending = 1;
            return true;
        }

        state->blasNode = RayQueryState::kInvalidNode;
        state->blasLeafOffset = 0;
    }

    return false;
}

} // namespace rhi::cpu

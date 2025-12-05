#include "vk-acceleration-structure.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-utils.h"

namespace rhi::vk {

AccelerationStructureImpl::AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc)
    : AccelerationStructure(device, desc)
{
}

AccelerationStructureImpl::~AccelerationStructureImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    if (device)
    {
        device->m_api.vkDestroyAccelerationStructureKHR(device->m_api.m_device, m_vkHandle, nullptr);
    }
}

Result AccelerationStructureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkAccelerationStructureKHR;
    outHandle->value = (uint64_t)m_vkHandle;
    return SLANG_OK;
}

AccelerationStructureHandle AccelerationStructureImpl::getHandle()
{
    return AccelerationStructureHandle{m_buffer->getDeviceAddress()};
}

DeviceAddress AccelerationStructureImpl::getDeviceAddress()
{
    return m_buffer->getDeviceAddress();
}

Result AccelerationStructureImpl::getDescriptorHandle(DescriptorHandle* outHandle)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    if (!device->m_bindlessDescriptorSet)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    if (!m_descriptorHandle)
    {
        SLANG_RETURN_ON_FAIL(
            device->m_bindlessDescriptorSet->allocAccelerationStructureHandle(this, &m_descriptorHandle)
        );
    }

    *outHandle = m_descriptorHandle;
    return SLANG_OK;
}

Result AccelerationStructureBuildDescConverter::convert(
    const AccelerationStructureBuildDesc& buildDesc,
    IDebugCallback* debugCallback
)
{
    if (buildDesc.inputCount < 1)
    {
        return SLANG_E_INVALID_ARG;
    }

    if (buildDesc.motionOptions.keyCount > kMaxAccelerationStructureMotionKeyCount)
    {
        return SLANG_E_INVALID_ARG;
    }

    AccelerationStructureBuildInputType type = buildDesc.inputs[0].type;
    for (uint32_t i = 1; i < buildDesc.inputCount; ++i)
    {
        if (buildDesc.inputs[i].type != type)
        {
            return SLANG_E_INVALID_ARG;
        }
    }

    buildInfo.dstAccelerationStructure = VK_NULL_HANDLE;
    switch (buildDesc.mode)
    {
    case AccelerationStructureBuildMode::Build:
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        break;
    case AccelerationStructureBuildMode::Update:
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }
    buildInfo.flags = translateBuildFlags(buildDesc.flags);
    geometries.resize(buildDesc.inputCount);
    primitiveCounts.resize(buildDesc.inputCount);
    buildInfo.pGeometries = geometries.data();
    buildInfo.geometryCount = geometries.size();

    switch (type)
    {
    case AccelerationStructureBuildInputType::Instances:
    {
        if (buildDesc.inputCount > 1)
        {
            return SLANG_E_INVALID_ARG;
        }
        const AccelerationStructureBuildInputInstances& instances = buildDesc.inputs[0].instances;

        VkAccelerationStructureGeometryKHR& geometry = geometries[0];
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.arrayOfPointers = 0;
        geometry.geometry.instances.data.deviceAddress = instances.instanceBuffer.getDeviceAddress();

        primitiveCounts[0] = instances.instanceCount;

        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        break;
    }
    case AccelerationStructureBuildInputType::Triangles:
    {
        bool useMotion = is_set(buildDesc.flags, AccelerationStructureBuildFlags::CreateMotion);
        if (useMotion)
        {
            motionTrianglesDatas.resize(buildDesc.inputCount);
        }

        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputTriangles& triangles = buildDesc.inputs[i].triangles;
            if (triangles.vertexBufferCount > kMaxAccelerationStructureMotionKeyCount)
            {
                return SLANG_E_INVALID_ARG;
            }
            if (useMotion && triangles.vertexBufferCount < 2)
            {
                return SLANG_E_INVALID_ARG;
            }

            VkAccelerationStructureGeometryKHR& geometry = geometries[i];
            geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.flags = translateGeometryFlags(triangles.flags);
            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = getVkFormat(triangles.vertexFormat);
            geometry.geometry.triangles.vertexData.deviceAddress = triangles.vertexBuffers[0].getDeviceAddress();
            geometry.geometry.triangles.vertexStride = triangles.vertexStride;
            geometry.geometry.triangles.maxVertex = triangles.vertexCount - 1;
            if (triangles.indexBuffer)
            {
                geometry.geometry.triangles.indexType =
                    triangles.indexFormat == IndexFormat::Uint32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
                geometry.geometry.triangles.indexData.deviceAddress = triangles.indexBuffer.getDeviceAddress();
            }
            else
            {
                geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
                geometry.geometry.triangles.indexData.deviceAddress = 0;
            }
            geometry.geometry.triangles.transformData.deviceAddress =
                triangles.preTransformBuffer ? triangles.preTransformBuffer.getDeviceAddress() : 0;

            if (useMotion)
            {
                VkAccelerationStructureGeometryMotionTrianglesDataNV& motionData = motionTrianglesDatas[i];
                motionData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_MOTION_TRIANGLES_DATA_NV;
                motionData.pNext = nullptr;
                motionData.vertexData.deviceAddress = triangles.vertexBuffers[1].getDeviceAddress();

                geometry.geometry.triangles.pNext = &motionData;
            }

            primitiveCounts[i] = max(triangles.vertexCount, triangles.indexCount) / 3;
        }

        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        break;
    }
    case AccelerationStructureBuildInputType::ProceduralPrimitives:
    {
        // Motion is only supported for triangles in Vulkan
        if (is_set(buildDesc.flags, AccelerationStructureBuildFlags::CreateMotion))
        {
            return SLANG_E_NOT_AVAILABLE;
        }

        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputProceduralPrimitives& proceduralPrimitives =
                buildDesc.inputs[i].proceduralPrimitives;
            if (proceduralPrimitives.aabbBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            VkAccelerationStructureGeometryKHR& geometry = geometries[i];
            geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
            geometry.flags = translateGeometryFlags(proceduralPrimitives.flags);
            geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
            geometry.geometry.aabbs.data.deviceAddress = proceduralPrimitives.aabbBuffers[0].getDeviceAddress();
            geometry.geometry.aabbs.stride = proceduralPrimitives.aabbStride;

            primitiveCounts[i] = proceduralPrimitives.primitiveCount;
        }

        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        break;
    }
    case AccelerationStructureBuildInputType::Spheres:
    {
        // Motion is only supported for triangles in Vulkan
        if (is_set(buildDesc.flags, AccelerationStructureBuildFlags::CreateMotion))
        {
            return SLANG_E_NOT_AVAILABLE;
        }

        spheresDatas.resize(buildDesc.inputCount);
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputSpheres& spheres = buildDesc.inputs[i].spheres;
            if (spheres.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            VkAccelerationStructureGeometrySpheresDataNV& spheresData = spheresDatas[i];
            spheresData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_SPHERES_DATA_NV;

            spheresData.vertexFormat = getVkFormat(spheres.vertexPositionFormat);
            spheresData.vertexData.deviceAddress = spheres.vertexPositionBuffers[0].getDeviceAddress();
            spheresData.vertexStride = spheres.vertexPositionStride;
            spheresData.radiusFormat = getVkFormat(spheres.vertexRadiusFormat);
            spheresData.radiusData.deviceAddress = spheres.vertexRadiusBuffers[0].getDeviceAddress();
            spheresData.radiusStride = spheres.vertexRadiusStride;
            if (spheres.indexBuffer)
            {
                spheresData.indexType =
                    spheres.indexFormat == IndexFormat::Uint32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
                spheresData.indexData.deviceAddress = spheres.indexBuffer.getDeviceAddress();
                spheresData.indexStride = spheres.indexFormat == IndexFormat::Uint32 ? 4 : 2;
            }
            else
            {
                spheresData.indexType = VK_INDEX_TYPE_NONE_KHR;
                spheresData.indexData.deviceAddress = 0;
                spheresData.indexStride = 0;
            }

            VkAccelerationStructureGeometryKHR& geometry = geometries[i];
            geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.pNext = &spheresDatas[i];
            geometry.geometryType = VK_GEOMETRY_TYPE_SPHERES_NV;
            geometry.flags = translateGeometryFlags(spheres.flags);

            primitiveCounts[i] = spheres.vertexCount;
        }

        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        break;
    }
    case AccelerationStructureBuildInputType::LinearSweptSpheres:
    {
        // Motion is only supported for triangles in Vulkan
        if (is_set(buildDesc.flags, AccelerationStructureBuildFlags::CreateMotion))
        {
            return SLANG_E_NOT_AVAILABLE;
        }

        linearSweptSpheresDatas.resize(buildDesc.inputCount);
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputLinearSweptSpheres& lss = buildDesc.inputs[i].linearSweptSpheres;
            if (lss.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            VkAccelerationStructureGeometryLinearSweptSpheresDataNV& lssData = linearSweptSpheresDatas[i];
            lssData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_LINEAR_SWEPT_SPHERES_DATA_NV;

            lssData.vertexFormat = getVkFormat(lss.vertexPositionFormat);
            lssData.vertexData.deviceAddress = lss.vertexPositionBuffers[0].getDeviceAddress();
            lssData.vertexStride = lss.vertexPositionStride;
            lssData.radiusFormat = getVkFormat(lss.vertexRadiusFormat);
            lssData.radiusData.deviceAddress = lss.vertexRadiusBuffers[0].getDeviceAddress();
            lssData.radiusStride = lss.vertexRadiusStride;
            if (lss.indexBuffer)
            {
                lssData.indexType =
                    lss.indexFormat == IndexFormat::Uint32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
                lssData.indexData.deviceAddress = lss.indexBuffer.getDeviceAddress();
                lssData.indexStride = lss.indexFormat == IndexFormat::Uint32 ? 4 : 2;
            }
            else
            {
                lssData.indexType = VK_INDEX_TYPE_NONE_KHR;
                lssData.indexData.deviceAddress = 0;
                lssData.indexStride = 0;
            }
            lssData.indexingMode = translateIndexingMode(lss.indexingMode);
            lssData.endCapsMode = translateEndCapsMode(lss.endCapsMode);

            VkAccelerationStructureGeometryKHR& geometry = geometries[i];
            geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.pNext = &linearSweptSpheresDatas[i];
            geometry.geometryType = VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV;
            geometry.flags = translateGeometryFlags(lss.flags);

            primitiveCounts[i] = lss.primitiveCount;
        }

        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        break;
    }
    default:
        return SLANG_E_INVALID_ARG;
    }

    return SLANG_OK;
}

VkBuildAccelerationStructureFlagsKHR AccelerationStructureBuildDescConverter::translateBuildFlags(
    AccelerationStructureBuildFlags flags
)
{
    VkBuildAccelerationStructureFlagsKHR result = VkBuildAccelerationStructureFlagsKHR(0);
    if (is_set(flags, AccelerationStructureBuildFlags::AllowCompaction))
    {
        result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    }
    if (is_set(flags, AccelerationStructureBuildFlags::AllowUpdate))
    {
        result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }
    if (is_set(flags, AccelerationStructureBuildFlags::MinimizeMemory))
    {
        result |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR;
    }
    if (is_set(flags, AccelerationStructureBuildFlags::PreferFastBuild))
    {
        result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    }
    if (is_set(flags, AccelerationStructureBuildFlags::PreferFastTrace))
    {
        result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    }
    if (is_set(flags, AccelerationStructureBuildFlags::CreateMotion))
    {
        result |= VK_BUILD_ACCELERATION_STRUCTURE_MOTION_BIT_NV;
    }
    return result;
}

VkGeometryFlagsKHR AccelerationStructureBuildDescConverter::translateGeometryFlags(
    AccelerationStructureGeometryFlags flags
)
{
    VkGeometryFlagsKHR result = VkGeometryFlagsKHR(0);
    if (is_set(flags, AccelerationStructureGeometryFlags::Opaque))
        result |= VK_GEOMETRY_OPAQUE_BIT_KHR;
    if (is_set(flags, AccelerationStructureGeometryFlags::NoDuplicateAnyHitInvocation))
        result |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
    return result;
}

VkRayTracingLssIndexingModeNV AccelerationStructureBuildDescConverter::translateIndexingMode(
    LinearSweptSpheresIndexingMode mode
)
{
    switch (mode)
    {
    case LinearSweptSpheresIndexingMode::List:
        return VK_RAY_TRACING_LSS_INDEXING_MODE_LIST_NV;
    case LinearSweptSpheresIndexingMode::Successive:
        return VK_RAY_TRACING_LSS_INDEXING_MODE_SUCCESSIVE_NV;
    default:
        return VkRayTracingLssIndexingModeNV(0);
    }
}

VkRayTracingLssPrimitiveEndCapsModeNV AccelerationStructureBuildDescConverter::translateEndCapsMode(
    LinearSweptSpheresEndCapsMode mode
)
{
    switch (mode)
    {
    case LinearSweptSpheresEndCapsMode::None:
        return VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_NONE_NV;
    case LinearSweptSpheresEndCapsMode::Chained:
        return VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_CHAINED_NV;
    default:
        return VkRayTracingLssPrimitiveEndCapsModeNV(0);
    }
}

VkBuildAccelerationStructureFlagsKHR translateClusterOperationFlags(ClusterOperationFlags flags)
{
    VkBuildAccelerationStructureFlagsKHR result = 0;
    if (is_set(flags, ClusterOperationFlags::FastTrace))
    {
        result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    }
    if (is_set(flags, ClusterOperationFlags::FastBuild))
    {
        result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    }
    if (is_set(flags, ClusterOperationFlags::AllowOMM))
    {
        result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DISABLE_OPACITY_MICROMAPS_BIT_EXT;
    }
    return result;
}

VkClusterAccelerationStructureTypeNV translateClusterOperationMoveType(ClusterOperationMoveType type)
{
    switch (type)
    {
    case ClusterOperationMoveType::BottomLevel:
        return VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV;
    case ClusterOperationMoveType::ClusterLevel:
        return VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_NV;
    case ClusterOperationMoveType::Template:
        return VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_TEMPLATE_NV;
    }
    return VkClusterAccelerationStructureTypeNV(0);
}

VkClusterAccelerationStructureInputInfoNV translateClusterOperationParams(
    const ClusterOperationParams& params,
    VkClusterAccelerationStructureClustersBottomLevelInputNV& bottomLevelInput,
    VkClusterAccelerationStructureTriangleClusterInputNV& triangleClusterInput,
    VkClusterAccelerationStructureMoveObjectsInputNV& moveObjectsInput
)
{
    VkClusterAccelerationStructureInputInfoNV info = {VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV};

    info.maxAccelerationStructureCount = params.maxArgCount;
    info.flags = translateClusterOperationFlags(params.flags);
    info.opMode = translateClusterOperationMode(params.mode);

    switch (params.type)
    {
    case ClusterOperationType::CLASFromTriangles:
        info.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
        break;
    case ClusterOperationType::BLASFromCLAS:
        info.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
        break;
    case ClusterOperationType::TemplatesFromTriangles:
        info.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV;
        break;
    case ClusterOperationType::CLASFromTemplates:
        info.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV;
        break;
    case ClusterOperationType::MoveObjects:
        info.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV;
        break;
    }

    bottomLevelInput = {VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV};
    triangleClusterInput = {VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV};
    moveObjectsInput = {VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_MOVE_OBJECTS_INPUT_NV};

    switch (info.opType)
    {
    case VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV:
        moveObjectsInput.type = translateClusterOperationMoveType(params.move.type);
        moveObjectsInput.noMoveOverlap = is_set(params.flags, ClusterOperationFlags::NoOverlap) ? VK_TRUE : VK_FALSE;
        moveObjectsInput.maxMovedBytes = params.move.maxSize;
        info.opInput.pMoveObjects = &moveObjectsInput;
        break;
    case VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV:
        bottomLevelInput.maxTotalClusterCount = params.blas.maxTotalClasCount;
        bottomLevelInput.maxClusterCountPerAccelerationStructure = params.blas.maxClasCount;
        info.opInput.pClustersBottomLevel = &bottomLevelInput;
        break;
    case VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV:
    case VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV:
    case VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV:
        triangleClusterInput.maxGeometryIndexValue = params.clas.maxGeometryIndex;
        triangleClusterInput.vertexFormat = getVkFormat(params.clas.vertexFormat);
        triangleClusterInput.maxGeometryIndexValue = params.clas.maxGeometryIndex;
        triangleClusterInput.maxClusterUniqueGeometryCount = params.clas.maxUniqueGeometryCount;
        triangleClusterInput.maxClusterTriangleCount = params.clas.maxTriangleCount;
        triangleClusterInput.maxClusterVertexCount = params.clas.maxVertexCount;
        triangleClusterInput.maxTotalTriangleCount = params.clas.maxTotalTriangleCount;
        triangleClusterInput.maxTotalVertexCount = params.clas.maxTotalVertexCount;
        triangleClusterInput.minPositionTruncateBitCount = params.clas.minPositionTruncateBitCount;
        info.opInput.pTriangleClusters = &triangleClusterInput;
        break;
    default:
        break;
    }

    return info;
}

VkClusterAccelerationStructureOpModeNV translateClusterOperationMode(ClusterOperationMode mode)
{
    switch (mode)
    {
    case ClusterOperationMode::ImplicitDestinations:
        return VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
    case ClusterOperationMode::ExplicitDestinations:
        return VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV;
    case ClusterOperationMode::GetSizes:
        return VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV;
    }
    return VkClusterAccelerationStructureOpModeNV(0);
}

} // namespace rhi::vk

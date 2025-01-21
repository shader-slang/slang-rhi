#include "vk-acceleration-structure.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-util.h"

namespace rhi::vk {

AccelerationStructureImpl::~AccelerationStructureImpl()
{
    if (m_device)
    {
        m_device->m_api.vkDestroyAccelerationStructureKHR(m_device->m_api.m_device, m_vkHandle, nullptr);
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

Result AccelerationStructureBuildGeometryInfoBuilder::build(
    const AccelerationStructureBuildDesc& buildDesc,
    IDebugCallback* debugCallback
)
{
    if (buildDesc.inputCount < 1)
    {
        return SLANG_E_INVALID_ARG;
    }

    AccelerationStructureBuildInputType type = (AccelerationStructureBuildInputType&)buildDesc.inputs[0];
    for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
    {
        if ((AccelerationStructureBuildInputType&)buildDesc.inputs[i] != type)
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
        const AccelerationStructureBuildInputInstances& instances =
            (const AccelerationStructureBuildInputInstances&)buildDesc.inputs[0];

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
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputTriangles& triangles =
                (const AccelerationStructureBuildInputTriangles&)buildDesc.inputs[i];
            if (triangles.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            VkAccelerationStructureGeometryKHR& geometry = geometries[i];
            geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.flags = translateGeometryFlags(triangles.flags);
            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = VulkanUtil::getVkFormat(triangles.vertexFormat);
            geometry.geometry.triangles.vertexData.deviceAddress = triangles.vertexBuffers[0].getDeviceAddress();
            geometry.geometry.triangles.vertexStride = triangles.vertexStride;
            geometry.geometry.triangles.maxVertex = triangles.vertexCount - 1;
            if (triangles.indexBuffer)
            {
                geometry.geometry.triangles.indexType =
                    triangles.indexFormat == IndexFormat::UInt32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
                geometry.geometry.triangles.indexData.deviceAddress = triangles.indexBuffer.getDeviceAddress();
            }
            else
            {
                geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
                geometry.geometry.triangles.indexData.deviceAddress = 0;
            }
            geometry.geometry.triangles.transformData.deviceAddress =
                triangles.preTransformBuffer ? triangles.preTransformBuffer.getDeviceAddress() : 0;

            primitiveCounts[i] = max(triangles.vertexCount, triangles.indexCount) / 3;
        }

        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        break;
    }
    case AccelerationStructureBuildInputType::ProceduralPrimitives:
    {
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputProceduralPrimitives& proceduralPrimitives =
                (const AccelerationStructureBuildInputProceduralPrimitives&)buildDesc.inputs[i];
            if (proceduralPrimitives.aabbBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            VkAccelerationStructureGeometryKHR& geometry = geometries[i];
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
    default:
        return SLANG_E_INVALID_ARG;
    }

    return SLANG_OK;
}

} // namespace rhi::vk

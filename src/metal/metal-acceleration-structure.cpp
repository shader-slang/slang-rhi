#include "metal-acceleration-structure.h"
#include "metal-device.h"
#include "metal-buffer.h"
#include "metal-utils.h"

namespace rhi::metal {

AccelerationStructureImpl::AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc)
    : AccelerationStructure(device, desc)
{
}

AccelerationStructureImpl::~AccelerationStructureImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    device->m_accelerationStructures.freeList.push_back(m_globalIndex);
    device->m_accelerationStructures.list[m_globalIndex] = nullptr;
    device->m_accelerationStructures.dirty = true;
}

Result AccelerationStructureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLAccelerationStructure;
    outHandle->value = (uint64_t)m_accelerationStructure.get();
    return SLANG_OK;
}

AccelerationStructureHandle AccelerationStructureImpl::getHandle()
{
    return AccelerationStructureHandle{m_globalIndex};
}

DeviceAddress AccelerationStructureImpl::getDeviceAddress()
{
    return 0;
}

Result DeviceImpl::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    AUTORELEASEPOOL

    RefPtr<AccelerationStructureImpl> result = new AccelerationStructureImpl(this, desc);
    result->m_accelerationStructure = NS::TransferPtr(m_device->newAccelerationStructure(desc.size));

    uint32_t globalIndex = 0;
    if (!m_accelerationStructures.freeList.empty())
    {
        globalIndex = m_accelerationStructures.freeList.back();
        m_accelerationStructures.freeList.pop_back();
        m_accelerationStructures.list[globalIndex] = result->m_accelerationStructure.get();
    }
    else
    {
        globalIndex = m_accelerationStructures.list.size();
        m_accelerationStructures.list.push_back(result->m_accelerationStructure.get());
    }
    m_accelerationStructures.dirty = true;
    result->m_globalIndex = globalIndex;

    returnComPtr(outAccelerationStructure, result);
    return SLANG_OK;
}

Result AccelerationStructureBuildDescConverter::convert(
    const AccelerationStructureBuildDesc& buildDesc,
    const NS::Array* accelerationStructureArray,
    IDebugCallback* debugCallback
)
{
    if (buildDesc.inputCount < 1)
    {
        return SLANG_E_INVALID_ARG;
    }

    // Motion blur is not supported in Metal
    if (is_set(buildDesc.flags, AccelerationStructureBuildFlags::CreateMotion))
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    AccelerationStructureBuildInputType type = buildDesc.inputs[0].type;
    for (uint32_t i = 1; i < buildDesc.inputCount; ++i)
    {
        if (buildDesc.inputs[i].type != type)
        {
            return SLANG_E_INVALID_ARG;
        }
    }

    switch (type)
    {
    case AccelerationStructureBuildInputType::Instances:
    {
        if (buildDesc.inputCount > 1)
        {
            return SLANG_E_INVALID_ARG;
        }

        const AccelerationStructureBuildInputInstances& instances = buildDesc.inputs[0].instances;

        MTL::InstanceAccelerationStructureDescriptor* instanceDescriptor =
            MTL::InstanceAccelerationStructureDescriptor::alloc()->init();
        descriptor = NS::TransferPtr(instanceDescriptor);

        instanceDescriptor->setUsage(translateBuildFlags(buildDesc.flags));
        instanceDescriptor->setInstanceDescriptorBuffer(
            checked_cast<BufferImpl*>(instances.instanceBuffer.buffer)->m_buffer.get()
        );
        instanceDescriptor->setInstanceDescriptorBufferOffset(instances.instanceBuffer.offset);
        instanceDescriptor->setInstanceDescriptorStride(instances.instanceStride);
        instanceDescriptor->setInstanceCount(instances.instanceCount);
        instanceDescriptor->setInstanceDescriptorType(MTL::AccelerationStructureInstanceDescriptorTypeUserID);
        instanceDescriptor->setInstancedAccelerationStructures(accelerationStructureArray);

        break;
    }
    case AccelerationStructureBuildInputType::Triangles:
    {
        MTL::PrimitiveAccelerationStructureDescriptor* primitiveDescriptor =
            MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init();
        descriptor = NS::TransferPtr(primitiveDescriptor);

        primitiveDescriptor->setUsage(translateBuildFlags(buildDesc.flags));

        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputTriangles& triangles = buildDesc.inputs[i].triangles;
            if (triangles.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            MTL::AccelerationStructureTriangleGeometryDescriptor* triangleDescriptor =
                (MTL::AccelerationStructureTriangleGeometryDescriptor*)primitiveDescriptor->geometryDescriptors()
                    ->object(i);

            triangleDescriptor->setVertexBuffer(
                checked_cast<BufferImpl*>(triangles.vertexBuffers[0].buffer)->m_buffer.get()
            );
            triangleDescriptor->setVertexBufferOffset(triangles.vertexBuffers[0].offset);
            triangleDescriptor->setVertexFormat(translateAttributeFormat(triangles.vertexFormat));
            triangleDescriptor->setVertexStride(triangles.vertexStride);

            if (triangles.indexBuffer)
            {
                triangleDescriptor->setIndexBuffer(
                    checked_cast<BufferImpl*>(triangles.indexBuffer.buffer)->m_buffer.get()
                );
                triangleDescriptor->setIndexBufferOffset(triangles.indexBuffer.offset);
                triangleDescriptor->setIndexType(
                    triangles.indexFormat == IndexFormat::Uint32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16
                );
            }

            uint32_t triangleCount = max(triangles.vertexCount, triangles.indexCount) / 3;
            triangleDescriptor->setTriangleCount(triangleCount);

            if (triangles.preTransformBuffer)
            {
                triangleDescriptor->setTransformationMatrixBuffer(
                    checked_cast<BufferImpl*>(triangles.preTransformBuffer.buffer)->m_buffer.get()
                );
                triangleDescriptor->setTransformationMatrixBufferOffset(triangles.preTransformBuffer.offset);
            }

            triangleDescriptor->setOpaque(is_set(triangles.flags, AccelerationStructureGeometryFlags::Opaque));
            triangleDescriptor->setAllowDuplicateIntersectionFunctionInvocation(
                !is_set(triangles.flags, AccelerationStructureGeometryFlags::NoDuplicateAnyHitInvocation)
            );
        }

        break;
    }
    case AccelerationStructureBuildInputType::ProceduralPrimitives:
    {
        MTL::PrimitiveAccelerationStructureDescriptor* primitiveDescriptor =
            MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init();
        descriptor = NS::TransferPtr(primitiveDescriptor);

        primitiveDescriptor->setUsage(translateBuildFlags(buildDesc.flags));

        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputProceduralPrimitives& proceduralPrimitives =
                buildDesc.inputs[i].proceduralPrimitives;
            if (proceduralPrimitives.aabbBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            MTL::AccelerationStructureBoundingBoxGeometryDescriptor* boundingDescriptor =
                (MTL::AccelerationStructureBoundingBoxGeometryDescriptor*)primitiveDescriptor->geometryDescriptors()
                    ->object(i);

            boundingDescriptor->setBoundingBoxBuffer(
                checked_cast<BufferImpl*>(proceduralPrimitives.aabbBuffers[0].buffer)->m_buffer.get()
            );
            boundingDescriptor->setBoundingBoxBufferOffset(proceduralPrimitives.aabbBuffers[0].offset);
            boundingDescriptor->setBoundingBoxStride(proceduralPrimitives.aabbStride);
            boundingDescriptor->setBoundingBoxCount(proceduralPrimitives.primitiveCount);
        }
        break;
    }
    case AccelerationStructureBuildInputType::Spheres:
    case AccelerationStructureBuildInputType::LinearSweptSpheres:
        return SLANG_E_NOT_AVAILABLE;
    default:
        return SLANG_E_INVALID_ARG;
    }

    return SLANG_OK;
}

} // namespace rhi::metal

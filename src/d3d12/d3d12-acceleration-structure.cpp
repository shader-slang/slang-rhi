#include "d3d12-acceleration-structure.h"
#include "d3d12-buffer.h"
#include "d3d12-device.h"

namespace rhi::d3d12 {

#if SLANG_RHI_DXR

AccelerationStructureImpl::~AccelerationStructureImpl()
{
    if (m_descriptor)
        m_device->m_cpuCbvSrvUavHeap->free(m_descriptor);
}

Result AccelerationStructureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12DeviceAddress;
    outHandle->value = getDeviceAddress();
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

Result AccelerationStructureInputsBuilder::build(
    const AccelerationStructureBuildDesc& buildDesc,
    IDebugCallback* callback
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

    desc.Flags = translateBuildFlags(buildDesc.flags);
    switch (buildDesc.mode)
    {
    case AccelerationStructureBuildMode::Build:
        desc.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        break;
    case AccelerationStructureBuildMode::Update:
        desc.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }

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
        desc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        desc.NumDescs = 1;
        desc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        desc.InstanceDescs = instances.instanceBuffer.getDeviceAddress();
        break;
    }
    case AccelerationStructureBuildInputType::Triangles:
    {
        geomDescs.resize(buildDesc.inputCount);
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputTriangles& triangles =
                (const AccelerationStructureBuildInputTriangles&)buildDesc.inputs[i];
            if (triangles.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }
            D3D12_RAYTRACING_GEOMETRY_DESC& geomDesc = geomDescs[i];
            geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geomDesc.Flags = translateGeometryFlags(triangles.flags);
            geomDesc.Triangles.VertexBuffer.StartAddress = triangles.vertexBuffers[0].getDeviceAddress();
            geomDesc.Triangles.VertexBuffer.StrideInBytes = triangles.vertexStride;
            geomDesc.Triangles.VertexCount = triangles.vertexCount;
            geomDesc.Triangles.VertexFormat = D3DUtil::getMapFormat(triangles.vertexFormat);
            if (triangles.indexBuffer)
            {
                geomDesc.Triangles.IndexBuffer = triangles.indexBuffer.getDeviceAddress();
                geomDesc.Triangles.IndexCount = triangles.indexCount;
                geomDesc.Triangles.IndexFormat = D3DUtil::getIndexFormat(triangles.indexFormat);
            }
            else
            {
                geomDesc.Triangles.IndexBuffer = 0;
                geomDesc.Triangles.IndexCount = 0;
                geomDesc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
            }
            geomDesc.Triangles.Transform3x4 =
                triangles.preTransformBuffer ? triangles.preTransformBuffer.getDeviceAddress() : 0;
        }
        desc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        desc.NumDescs = geomDescs.size();
        desc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        desc.pGeometryDescs = geomDescs.data();
        break;
    }
    case AccelerationStructureBuildInputType::ProceduralPrimitives:
    {
        geomDescs.resize(buildDesc.inputCount);
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputProceduralPrimitives& proceduralPrimitives =
                (const AccelerationStructureBuildInputProceduralPrimitives&)buildDesc.inputs[i];
            if (proceduralPrimitives.aabbBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }
            D3D12_RAYTRACING_GEOMETRY_DESC& geomDesc = geomDescs[i];
            geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
            geomDesc.Flags = translateGeometryFlags(proceduralPrimitives.flags);
            geomDesc.AABBs.AABBCount = proceduralPrimitives.primitiveCount;
            geomDesc.AABBs.AABBs.StartAddress = proceduralPrimitives.aabbBuffers[0].getDeviceAddress();
            geomDesc.AABBs.AABBs.StrideInBytes = proceduralPrimitives.aabbStride;
        }
        desc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        desc.NumDescs = geomDescs.size();
        desc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        desc.pGeometryDescs = geomDescs.data();
        break;
    }
    default:
        return SLANG_E_INVALID_ARG;
    }

    return SLANG_OK;
}

#endif // SLANG_RHI_DXR

} // namespace rhi::d3d12

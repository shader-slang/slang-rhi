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

Result AccelerationStructureBuildDescConverter::convert(
    const AccelerationStructureBuildDesc& buildDesc,
    IDebugCallback* callback
)
{
    if (buildDesc.inputCount < 1)
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
        const AccelerationStructureBuildInputInstances& instances = buildDesc.inputs[0].instances;
        desc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        desc.NumDescs = buildDesc.inputs[0].instances.instanceCount;
        desc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        desc.InstanceDescs = instances.instanceBuffer.getDeviceAddress();
        break;
    }
    case AccelerationStructureBuildInputType::Triangles:
    {
        geomDescs.resize(buildDesc.inputCount);
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputTriangles& triangles = buildDesc.inputs[i].triangles;
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
                buildDesc.inputs[i].proceduralPrimitives;
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

#if SLANG_RHI_ENABLE_NVAPI
Result AccelerationStructureBuildDescConverterNVAPI::convert(
    const AccelerationStructureBuildDesc& buildDesc,
    IDebugCallback* callback
)
{
    if (buildDesc.inputCount < 1)
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

    desc.flags = translateBuildFlags(buildDesc.flags);
    switch (buildDesc.mode)
    {
    case AccelerationStructureBuildMode::Build:
        break;
    case AccelerationStructureBuildMode::Update:
        (int&)desc.flags |= NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE_EX;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }

    desc.geometryDescStrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX);

    switch (type)
    {
    case AccelerationStructureBuildInputType::Instances:
    {
        if (buildDesc.inputCount > 1)
        {
            return SLANG_E_INVALID_ARG;
        }
        const AccelerationStructureBuildInputInstances& instances = buildDesc.inputs[0].instances;
        desc.type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        desc.numDescs = buildDesc.inputs[0].instances.instanceCount;
        desc.descsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        desc.instanceDescs = instances.instanceBuffer.getDeviceAddress();
        break;
    }
    case AccelerationStructureBuildInputType::Triangles:
    {
        geomDescs.resize(buildDesc.inputCount);
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputTriangles& triangles = buildDesc.inputs[i].triangles;
            if (triangles.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }
            NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX& geomDesc = geomDescs[i];
            geomDesc.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES_EX;
            geomDesc.flags = translateGeometryFlags(triangles.flags);
            geomDesc.triangles.VertexBuffer.StartAddress = triangles.vertexBuffers[0].getDeviceAddress();
            geomDesc.triangles.VertexBuffer.StrideInBytes = triangles.vertexStride;
            geomDesc.triangles.VertexCount = triangles.vertexCount;
            geomDesc.triangles.VertexFormat = D3DUtil::getMapFormat(triangles.vertexFormat);
            if (triangles.indexBuffer)
            {
                geomDesc.triangles.IndexBuffer = triangles.indexBuffer.getDeviceAddress();
                geomDesc.triangles.IndexCount = triangles.indexCount;
                geomDesc.triangles.IndexFormat = D3DUtil::getIndexFormat(triangles.indexFormat);
            }
            else
            {
                geomDesc.triangles.IndexBuffer = 0;
                geomDesc.triangles.IndexCount = 0;
                geomDesc.triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
            }
            geomDesc.triangles.Transform3x4 =
                triangles.preTransformBuffer ? triangles.preTransformBuffer.getDeviceAddress() : 0;
        }
        desc.type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        desc.numDescs = geomDescs.size();
        desc.descsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        desc.pGeometryDescs = geomDescs.data();
        break;
    }
    case AccelerationStructureBuildInputType::ProceduralPrimitives:
    {
        geomDescs.resize(buildDesc.inputCount);
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputProceduralPrimitives& proceduralPrimitives =
                buildDesc.inputs[i].proceduralPrimitives;
            if (proceduralPrimitives.aabbBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }
            NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX& geomDesc = geomDescs[i];
            geomDesc.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS_EX;
            geomDesc.flags = translateGeometryFlags(proceduralPrimitives.flags);
            geomDesc.aabbs.AABBCount = proceduralPrimitives.primitiveCount;
            geomDesc.aabbs.AABBs.StartAddress = proceduralPrimitives.aabbBuffers[0].getDeviceAddress();
            geomDesc.aabbs.AABBs.StrideInBytes = proceduralPrimitives.aabbStride;
        }
        desc.type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        desc.numDescs = geomDescs.size();
        desc.descsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        desc.pGeometryDescs = geomDescs.data();
        break;
    }
    default:
        return SLANG_E_INVALID_ARG;
    }

    return SLANG_OK;
}
#endif // SLANG_RHI_ENABLE_NVAPI

#endif // SLANG_RHI_DXR

} // namespace rhi::d3d12

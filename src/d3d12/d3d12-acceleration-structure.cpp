#include "d3d12-acceleration-structure.h"
#include "d3d12-buffer.h"
#include "d3d12-device.h"

namespace rhi::d3d12 {

AccelerationStructureImpl::AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc)
    : AccelerationStructure(device, desc)
{
}

AccelerationStructureImpl::~AccelerationStructureImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    if (m_descriptorHandle)
    {
        device->m_bindlessDescriptorSet->freeHandle(m_descriptorHandle);
    }

    if (m_descriptor)
    {
        device->m_cpuCbvSrvUavHeap->free(m_descriptor);
    }
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
    IDebugCallback* callback
)
{
    if (buildDesc.inputCount < 1)
    {
        return SLANG_E_INVALID_ARG;
    }

    // Motion blur is not supported in D3D12
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
            geomDesc.Triangles.VertexFormat = getVertexFormat(triangles.vertexFormat);
            if (triangles.indexBuffer)
            {
                geomDesc.Triangles.IndexBuffer = triangles.indexBuffer.getDeviceAddress();
                geomDesc.Triangles.IndexCount = triangles.indexCount;
                geomDesc.Triangles.IndexFormat = getIndexFormat(triangles.indexFormat);
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
    case AccelerationStructureBuildInputType::Spheres:
    case AccelerationStructureBuildInputType::LinearSweptSpheres:
        return SLANG_E_NOT_AVAILABLE;
    default:
        return SLANG_E_INVALID_ARG;
    }

    return SLANG_OK;
}

D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS AccelerationStructureBuildDescConverter::translateBuildFlags(
    AccelerationStructureBuildFlags flags
)
{
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::None) == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE
    );
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::AllowUpdate) ==
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
    );
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::AllowCompaction) ==
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION
    );
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::PreferFastTrace) ==
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
    );
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::PreferFastBuild) ==
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
    );
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::MinimizeMemory) ==
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY
    );
    return (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)flags;
}

D3D12_RAYTRACING_GEOMETRY_FLAGS AccelerationStructureBuildDescConverter::translateGeometryFlags(
    AccelerationStructureGeometryFlags flags
)
{
    static_assert(uint32_t(AccelerationStructureGeometryFlags::None) == D3D12_RAYTRACING_GEOMETRY_FLAG_NONE);
    static_assert(uint32_t(AccelerationStructureGeometryFlags::Opaque) == D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE);
    static_assert(
        uint32_t(AccelerationStructureGeometryFlags::NoDuplicateAnyHitInvocation) ==
        D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION
    );
    return (D3D12_RAYTRACING_GEOMETRY_FLAGS)flags;
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

    // Motion blur is not supported in D3D12
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
            geomDesc.triangles.VertexFormat = getVertexFormat(triangles.vertexFormat);
            if (triangles.indexBuffer)
            {
                geomDesc.triangles.IndexBuffer = triangles.indexBuffer.getDeviceAddress();
                geomDesc.triangles.IndexCount = triangles.indexCount;
                geomDesc.triangles.IndexFormat = getIndexFormat(triangles.indexFormat);
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
    case AccelerationStructureBuildInputType::Spheres:
    {
        geomDescs.resize(buildDesc.inputCount);
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputSpheres& spheres = buildDesc.inputs[i].spheres;
            if (spheres.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }
            NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX& geomDesc = geomDescs[i];
            geomDesc.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_SPHERES_EX;
            geomDesc.flags = translateGeometryFlags(spheres.flags);
            geomDesc.spheres.vertexCount = spheres.vertexCount;
            geomDesc.spheres.vertexPositionBuffer.StartAddress = spheres.vertexPositionBuffers[0].getDeviceAddress();
            geomDesc.spheres.vertexPositionBuffer.StrideInBytes = spheres.vertexPositionStride;
            geomDesc.spheres.vertexPositionFormat = getVertexFormat(spheres.vertexPositionFormat);
            geomDesc.spheres.vertexRadiusBuffer.StartAddress = spheres.vertexRadiusBuffers[0].getDeviceAddress();
            geomDesc.spheres.vertexRadiusBuffer.StrideInBytes = spheres.vertexRadiusStride;
            geomDesc.spheres.vertexRadiusFormat = getVertexFormat(spheres.vertexRadiusFormat);
            if (spheres.indexBuffer)
            {
                geomDesc.spheres.indexCount = spheres.indexCount;
                geomDesc.spheres.indexBuffer.StartAddress = spheres.indexBuffer.getDeviceAddress();
                geomDesc.spheres.indexBuffer.StrideInBytes = spheres.indexFormat == IndexFormat::Uint32 ? 4 : 2;
                geomDesc.spheres.indexFormat = getIndexFormat(spheres.indexFormat);
            }
            else
            {
                geomDesc.spheres.indexCount = 0;
                geomDesc.spheres.indexBuffer.StartAddress = 0;
                geomDesc.spheres.indexBuffer.StrideInBytes = 0;
                geomDesc.spheres.indexFormat = DXGI_FORMAT_UNKNOWN;
            }
        }
        desc.type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        desc.numDescs = geomDescs.size();
        desc.descsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        desc.pGeometryDescs = geomDescs.data();
        break;
    }
    case AccelerationStructureBuildInputType::LinearSweptSpheres:
    {
        geomDescs.resize(buildDesc.inputCount);
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputLinearSweptSpheres& lss = buildDesc.inputs[i].linearSweptSpheres;
            if (lss.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }
            NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX& geomDesc = geomDescs[i];
            geomDesc.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_LSS_EX;
            geomDesc.flags = translateGeometryFlags(lss.flags);
            geomDesc.lss.vertexCount = lss.vertexCount;
            geomDesc.lss.vertexPositionBuffer.StartAddress = lss.vertexPositionBuffers[0].getDeviceAddress();
            geomDesc.lss.vertexPositionBuffer.StrideInBytes = lss.vertexPositionStride;
            geomDesc.lss.vertexPositionFormat = getVertexFormat(lss.vertexPositionFormat);
            geomDesc.lss.vertexRadiusBuffer.StartAddress = lss.vertexRadiusBuffers[0].getDeviceAddress();
            geomDesc.lss.vertexRadiusBuffer.StrideInBytes = lss.vertexRadiusStride;
            geomDesc.lss.vertexRadiusFormat = getVertexFormat(lss.vertexRadiusFormat);
            if (lss.indexBuffer)
            {
                geomDesc.lss.indexCount = lss.indexCount;
                geomDesc.lss.indexBuffer.StartAddress = lss.indexBuffer.getDeviceAddress();
                geomDesc.lss.indexBuffer.StrideInBytes = lss.indexFormat == IndexFormat::Uint32 ? 4 : 2;
                geomDesc.lss.indexFormat = getIndexFormat(lss.indexFormat);
            }
            else
            {
                geomDesc.lss.indexCount = 0;
                geomDesc.lss.indexBuffer.StartAddress = 0;
                geomDesc.lss.indexBuffer.StrideInBytes = 0;
                geomDesc.lss.indexFormat = DXGI_FORMAT_UNKNOWN;
            }
            geomDesc.lss.primitiveCount = lss.primitiveCount;
            geomDesc.lss.primitiveFormat = translateIndexingMode(lss.indexingMode);
            geomDesc.lss.endcapMode = translateEndCapsMode(lss.endCapsMode);
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

NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS_EX AccelerationStructureBuildDescConverterNVAPI::
    translateBuildFlags(AccelerationStructureBuildFlags flags)
{
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::None) ==
        NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE_EX
    );
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::AllowUpdate) ==
        NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE_EX
    );
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::AllowCompaction) ==
        NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION_EX
    );
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::PreferFastTrace) ==
        NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE_EX
    );
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::PreferFastBuild) ==
        NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD_EX
    );
    static_assert(
        uint32_t(AccelerationStructureBuildFlags::MinimizeMemory) ==
        NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY_EX
    );
    return (NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS_EX)flags;
}

D3D12_RAYTRACING_GEOMETRY_FLAGS AccelerationStructureBuildDescConverterNVAPI::translateGeometryFlags(
    AccelerationStructureGeometryFlags flags
)
{
    static_assert(uint32_t(AccelerationStructureGeometryFlags::None) == D3D12_RAYTRACING_GEOMETRY_FLAG_NONE);
    static_assert(uint32_t(AccelerationStructureGeometryFlags::Opaque) == D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE);
    static_assert(
        uint32_t(AccelerationStructureGeometryFlags::NoDuplicateAnyHitInvocation) ==
        D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION
    );
    return (D3D12_RAYTRACING_GEOMETRY_FLAGS)flags;
}

NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT AccelerationStructureBuildDescConverterNVAPI::translateIndexingMode(
    LinearSweptSpheresIndexingMode mode
)
{
    switch (mode)
    {
    case LinearSweptSpheresIndexingMode::List:
        return NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_LIST;
    case LinearSweptSpheresIndexingMode::Successive:
        return NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_SUCCESSIVE_IMPLICIT;
    default:
        return NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT(0);
    }
}

NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE AccelerationStructureBuildDescConverterNVAPI::translateEndCapsMode(
    LinearSweptSpheresEndCapsMode mode
)
{
    switch (mode)
    {
    case LinearSweptSpheresEndCapsMode::None:
        return NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_NONE;
    case LinearSweptSpheresEndCapsMode::Chained:
        return NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED;
    default:
        return NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE(0);
    }
}

NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAGS translateClusterOperationFlags(
    ClusterOperationFlags flags
)
{
    unsigned int result = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE;
    if (is_set(flags, ClusterOperationFlags::FastTrace))
    {
        result |= NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE;
    }
    if (is_set(flags, ClusterOperationFlags::FastBuild))
    {
        result |= NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_BUILD;
    }
    if (is_set(flags, ClusterOperationFlags::NoOverlap))
    {
        result |= NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NO_OVERLAP;
    }
    if (is_set(flags, ClusterOperationFlags::AllowOMM))
    {
        result |= NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM;
    }
    return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAGS(result);
}

NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE translateClusterOperationMoveType(
    ClusterOperationMoveType type
)
{
    switch (type)
    {
    case ClusterOperationMoveType::BottomLevel:
        return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE_BOTTOM_LEVEL_ACCELERATION_STRUCTURE;
    case ClusterOperationMoveType::ClusterLevel:
        return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE_CLUSTER_LEVEL_ACCELERATION_STRUCTURE;
    case ClusterOperationMoveType::Template:
        return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE_TEMPLATE;
    }
    return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE(0);
}

NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS translateClusterOperationParams(
    const ClusterOperationParams& params
)
{
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS inputs = {};

    inputs.maxArgCount = params.maxArgCount;
    inputs.flags = translateClusterOperationFlags(params.flags);
    inputs.mode = translateClusterOperationMode(params.mode);

    switch (params.type)
    {
    case ClusterOperationType::CLASFromTriangles:
        inputs.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES;
        break;
    case ClusterOperationType::BLASFromCLAS:
        inputs.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS;
        break;
    case ClusterOperationType::TemplatesFromTriangles:
        inputs.type =
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES;
        break;
    case ClusterOperationType::CLASFromTemplates:
        inputs.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_INSTANTIATE_CLUSTER_TEMPLATES;
        break;
    case ClusterOperationType::MoveObjects:
        inputs.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_MOVE_CLUSTER_OBJECT;
        break;
    }

    switch (inputs.type)
    {
    case NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_MOVE_CLUSTER_OBJECT:
        inputs.movesDesc.type = translateClusterOperationMoveType(params.move.type);
        inputs.movesDesc.maxBytesMoved = params.move.maxSize;
        break;
    case NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS:
        inputs.clasDesc.maxTotalClasCount = params.blas.maxTotalClasCount;
        inputs.clasDesc.maxClasCountPerArg = params.blas.maxClasCount;
        break;
    case NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES:
    case NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES:
    case NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_INSTANTIATE_CLUSTER_TEMPLATES:
        inputs.trianglesDesc.vertexFormat = getVertexFormat(params.clas.vertexFormat);
        inputs.trianglesDesc.maxGeometryIndexValue = params.clas.maxGeometryIndex;
        inputs.trianglesDesc.maxUniqueGeometryCountPerArg = params.clas.maxUniqueGeometryCount;
        inputs.trianglesDesc.maxTriangleCountPerArg = params.clas.maxTriangleCount;
        inputs.trianglesDesc.maxVertexCountPerArg = params.clas.maxVertexCount;
        inputs.trianglesDesc.maxTotalTriangleCount = params.clas.maxTotalTriangleCount;
        inputs.trianglesDesc.maxTotalVertexCount = params.clas.maxTotalVertexCount;
        inputs.trianglesDesc.minPositionTruncateBitCount = params.clas.minPositionTruncateBitCount;
        break;
    default:
        break;
    }

    return inputs;
}

NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE translateClusterOperationMode(ClusterOperationMode mode)
{
    switch (mode)
    {
    case ClusterOperationMode::ImplicitDestinations:
        return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS;
    case ClusterOperationMode::ExplicitDestinations:
        return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_EXPLICIT_DESTINATIONS;
    case ClusterOperationMode::GetSizes:
        return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_GET_SIZES;
    }
    return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE(0);
}

#endif // SLANG_RHI_ENABLE_NVAPI

} // namespace rhi::d3d12

#include "cuda-acceleration-structure.h"
#include "cuda-device.h"

#if SLANG_RHI_ENABLE_OPTIX

namespace rhi::cuda {

AccelerationStructureImpl::AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc)
    : AccelerationStructure(device, desc)
{
}

AccelerationStructureImpl::~AccelerationStructureImpl()
{
    SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(m_buffer));
    SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(m_propertyBuffer));
}

Result AccelerationStructureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::OptixTraversableHandle;
    outHandle->value = (uint64_t)m_handle;
    return SLANG_OK;
}

AccelerationStructureHandle AccelerationStructureImpl::getHandle()
{
    return AccelerationStructureHandle{m_handle};
}

DeviceAddress AccelerationStructureImpl::getDeviceAddress()
{
    return m_buffer;
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

    AccelerationStructureBuildInputType type = buildDesc.inputs[0].type;
    for (uint32_t i = 1; i < buildDesc.inputCount; ++i)
    {
        if (buildDesc.inputs[i].type != type)
        {
            return SLANG_E_INVALID_ARG;
        }
    }

    buildOptions.buildFlags = translateBuildFlags(buildDesc.flags);
    buildOptions.motionOptions.numKeys = buildDesc.motionOptions.keyCount;
    buildOptions.motionOptions.flags = OPTIX_MOTION_FLAG_NONE;
    buildOptions.motionOptions.timeBegin = buildDesc.motionOptions.timeStart;
    buildOptions.motionOptions.timeEnd = buildDesc.motionOptions.timeEnd;
    switch (buildDesc.mode)
    {
    case AccelerationStructureBuildMode::Build:
        buildOptions.operation = OPTIX_BUILD_OPERATION_BUILD;
        break;
    case AccelerationStructureBuildMode::Update:
        buildOptions.operation = OPTIX_BUILD_OPERATION_UPDATE;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }

    buildInputs.resize(buildDesc.inputCount);

    switch (type)
    {
    case AccelerationStructureBuildInputType::Instances:
    {
        if (buildDesc.inputCount > 1)
        {
            return SLANG_E_INVALID_ARG;
        }
        const AccelerationStructureBuildInputInstances& instances = buildDesc.inputs[0].instances;
        OptixBuildInput& buildInput = buildInputs[0];
        buildInput = {};
        buildInput.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
        buildInput.instanceArray.instances = instances.instanceBuffer.getDeviceAddress();
        buildInput.instanceArray.instanceStride = instances.instanceStride;
        buildInput.instanceArray.numInstances = instances.instanceCount;
        break;
    }
    case AccelerationStructureBuildInputType::Triangles:
    {
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputTriangles& triangles = buildDesc.inputs[i].triangles;
            if (triangles.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            OptixBuildInput& buildInput = buildInputs[i];
            buildInput = {};
            buildInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;

            pointerList.push_back(triangles.vertexBuffers[0].getDeviceAddress());
            buildInput.triangleArray.vertexBuffers = &pointerList.back();
            buildInput.triangleArray.numVertices = triangles.vertexCount;
            buildInput.triangleArray.vertexFormat = translateVertexFormat(triangles.vertexFormat);
            buildInput.triangleArray.vertexStrideInBytes = triangles.vertexStride;
            if (triangles.indexBuffer)
            {
                buildInput.triangleArray.indexBuffer = triangles.indexBuffer.getDeviceAddress();
                buildInput.triangleArray.numIndexTriplets = triangles.indexCount / 3;
                buildInput.triangleArray.indexFormat = triangles.indexFormat == IndexFormat::Uint32
                                                           ? OPTIX_INDICES_FORMAT_UNSIGNED_INT3
                                                           : OPTIX_INDICES_FORMAT_UNSIGNED_SHORT3;
            }
            else
            {
                buildInput.triangleArray.indexBuffer = 0;
                buildInput.triangleArray.numIndexTriplets = 0;
                buildInput.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_NONE;
            }
            flagList.push_back(translateGeometryFlags(triangles.flags));
            buildInput.triangleArray.flags = &flagList.back();
            buildInput.triangleArray.numSbtRecords = 1;
            buildInput.triangleArray.preTransform =
                triangles.preTransformBuffer ? triangles.preTransformBuffer.getDeviceAddress() : 0;
            buildInput.triangleArray.transformFormat =
                triangles.preTransformBuffer ? OPTIX_TRANSFORM_FORMAT_MATRIX_FLOAT12 : OPTIX_TRANSFORM_FORMAT_NONE;
        }
        break;
    }
    case AccelerationStructureBuildInputType::ProceduralPrimitives:
    {
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputProceduralPrimitives& proceduralPrimitives =
                buildDesc.inputs[i].proceduralPrimitives;
            if (proceduralPrimitives.aabbBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }

            OptixBuildInput& buildInput = buildInputs[i];
            buildInput = {};
            buildInput.type = OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;

            pointerList.push_back(proceduralPrimitives.aabbBuffers[0].getDeviceAddress());
            buildInput.customPrimitiveArray.aabbBuffers = &pointerList.back();
            buildInput.customPrimitiveArray.numPrimitives = proceduralPrimitives.primitiveCount;
            buildInput.customPrimitiveArray.strideInBytes = proceduralPrimitives.aabbStride;
            buildInput.customPrimitiveArray.flags = &flagList.back();
            buildInput.customPrimitiveArray.numSbtRecords = 1;
        }
        break;
    }
    case AccelerationStructureBuildInputType::Spheres:
    {
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputSpheres& spheres = buildDesc.inputs[i].spheres;
            if (spheres.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }
            if (spheres.vertexPositionFormat != Format::R32G32B32_FLOAT)
            {
                return SLANG_E_INVALID_ARG;
            }
            if (spheres.vertexRadiusFormat != Format::R32_FLOAT)
            {
                return SLANG_E_INVALID_ARG;
            }
            if (spheres.indexBuffer)
            {
                return SLANG_E_INVALID_ARG;
            }

            OptixBuildInput& buildInput = buildInputs[i];
            buildInput = {};
            buildInput.type = OPTIX_BUILD_INPUT_TYPE_SPHERES;

            pointerList.push_back(spheres.vertexPositionBuffers[0].getDeviceAddress());
            buildInput.sphereArray.vertexBuffers = &pointerList.back();
            buildInput.sphereArray.vertexStrideInBytes = spheres.vertexPositionStride;
            buildInput.sphereArray.numVertices = spheres.vertexCount;
            pointerList.push_back(spheres.vertexRadiusBuffers[0].getDeviceAddress());
            buildInput.sphereArray.radiusBuffers = &pointerList.back();
            buildInput.sphereArray.radiusStrideInBytes = spheres.vertexRadiusStride;
            flagList.push_back(translateGeometryFlags(spheres.flags));
            buildInput.sphereArray.flags = &flagList.back();
            buildInput.sphereArray.numSbtRecords = 1;
        }
        break;
    }
    case AccelerationStructureBuildInputType::LinearSweptSpheres:
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    default:
        return SLANG_E_INVALID_ARG;
    }

    return SLANG_OK;
}

unsigned int AccelerationStructureBuildDescConverter::translateBuildFlags(AccelerationStructureBuildFlags flags) const
{
    unsigned int result = OPTIX_BUILD_FLAG_NONE;
    if (is_set(flags, AccelerationStructureBuildFlags::AllowCompaction))
    {
        result |= OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
    }
    if (is_set(flags, AccelerationStructureBuildFlags::AllowUpdate))
    {
        result |= OPTIX_BUILD_FLAG_ALLOW_UPDATE;
    }
    if (is_set(flags, AccelerationStructureBuildFlags::MinimizeMemory))
    {
        // result |= OPTIX_BUILD_FLAG_MINIMIZE_MEMORY;
    }
    if (is_set(flags, AccelerationStructureBuildFlags::PreferFastBuild))
    {
        result |= OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
    }
    if (is_set(flags, AccelerationStructureBuildFlags::PreferFastTrace))
    {
        result |= OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    }
    return result;
}

unsigned int AccelerationStructureBuildDescConverter::translateGeometryFlags(AccelerationStructureGeometryFlags flags
) const
{
    unsigned int result = 0;
    if (is_set(flags, AccelerationStructureGeometryFlags::Opaque))
    {
        result |= OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT;
    }
    if (is_set(flags, AccelerationStructureGeometryFlags::NoDuplicateAnyHitInvocation))
    {
        result |= OPTIX_GEOMETRY_FLAG_REQUIRE_SINGLE_ANYHIT_CALL;
    }
    return result;
}

OptixVertexFormat AccelerationStructureBuildDescConverter::translateVertexFormat(Format format) const
{
    switch (format)
    {
    case Format::R32G32B32_FLOAT:
        return OPTIX_VERTEX_FORMAT_FLOAT3;
    case Format::R32G32_FLOAT:
        return OPTIX_VERTEX_FORMAT_FLOAT2;
    case Format::R16G16_FLOAT:
        return OPTIX_VERTEX_FORMAT_HALF2;
    default:
        return OPTIX_VERTEX_FORMAT_NONE;
    }
}

} // namespace rhi::cuda

#endif // SLANG_RHI_ENABLE_OPTIX

#pragma once

#include "cuda-api.h"
#include "rhi-shared.h"

#include "core/assert.h"

namespace rhi::cuda {
class DeviceImpl;
class AccelerationStructureImpl;
class ShaderTableImpl;
class RayTracingPipelineImpl;
} // namespace rhi::cuda

namespace rhi::cuda::optix {

typedef unsigned long long OptixTraversableHandle;

class Pipeline;
class ShaderBindingTable;

class Context : public RefObject
{
public:
    virtual void* getOptixDeviceContext() const = 0;

    virtual Result createPipeline(
        const RayTracingPipelineDesc& desc,
        ShaderCompilationReporter* shaderCompilationReporter,
        Pipeline** outPipeline
    ) = 0;

    virtual Result createShaderBindingTable(
        ShaderTableImpl* shaderTable,
        Pipeline* pipeline,
        ShaderBindingTable** outShaderBindingTable
    ) = 0;

    virtual Result getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) = 0;

    virtual void buildAccelerationStructure(
        CUstream stream,
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureImpl* dst,
        AccelerationStructureImpl* src,
        BufferOffsetPair scratchBuffer,
        uint32_t propertyQueryCount,
        const AccelerationStructureQueryDesc* queryDescs
    ) = 0;

    virtual void copyAccelerationStructure(
        CUstream stream,
        AccelerationStructureImpl* dst,
        AccelerationStructureImpl* src,
        AccelerationStructureCopyMode mode
    ) = 0;

    virtual void dispatchRays(
        CUstream stream,
        Pipeline* pipeline,
        CUdeviceptr pipelineParams,
        size_t pipelineParamsSize,
        ShaderBindingTable* shaderBindingTable,
        uint32_t rayGenShaderIndex,
        uint32_t width,
        uint32_t height,
        uint32_t depth
    ) = 0;
};

class ShaderBindingTable : public RefObject
{
public:
};

class Pipeline : public RefObject
{
public:
    virtual uint64_t getNativeHandle() const = 0;
};

Result createContext(
    DeviceImpl* device,
    void* existingOptixDeviceContext,
    bool enableRayTracingValidation,
    Context** outContext
);

} // namespace rhi::cuda::optix

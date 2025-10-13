#pragma once

#include "cuda-api.h"
#include "core/smart-pointer.h"

// Forward declarations

namespace rhi {
class ShaderCompilationReporter;
}

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
    /// Get the OptiX version used by this context.
    /// The format matches the OPTIX_VERSION macro, e.g. 0x090000 for version 9.0.0.
    virtual int getOptixVersion() const = 0;

    /// Get the underlying OptiX device context.
    virtual void* getOptixDeviceContext() const = 0;

    /// Create a new OptiX pipeline.
    virtual Result createPipeline(
        const RayTracingPipelineDesc& desc,
        ShaderCompilationReporter* shaderCompilationReporter,
        Pipeline** outPipeline
    ) = 0;

    /// Create a new shader binding table for the given shader table and pipeline.
    virtual Result createShaderBindingTable(
        ShaderTableImpl* shaderTable,
        Pipeline* pipeline,
        ShaderBindingTable** outShaderBindingTable
    ) = 0;

    /// Get the sizes required for building an acceleration structure.
    virtual Result getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) = 0;

    /// Build an acceleration structure.
    virtual void buildAccelerationStructure(
        CUstream stream,
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureImpl* dst,
        AccelerationStructureImpl* src,
        CUdeviceptr scratchBuffer,
        size_t scratchBufferSize,
        uint32_t propertyQueryCount,
        const AccelerationStructureQueryDesc* queryDescs
    ) = 0;

    /// Copy an acceleration structure.
    virtual void copyAccelerationStructure(
        CUstream stream,
        AccelerationStructureImpl* dst,
        AccelerationStructureImpl* src,
        AccelerationStructureCopyMode mode
    ) = 0;

    /// Launch a ray tracing dispatch.
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
{};

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

#include "optix-api.h"
#include "cuda-device.h"
#include "cuda-buffer.h"
#include "cuda-acceleration-structure.h"
#include "cuda-query.h"
#include "cuda-shader-program.h"
#include "cuda-shader-table.h"
#include "cuda-pipeline.h"
#include "cuda-utils.h"

#include "core/stable_vector.h"
#include "core/string.h"

#define OPTIX_DONT_INCLUDE_CUDA
#define OPTIX_ENABLE_SDK_MIXING
#include <optix.h>
#include <optix_stubs.h>

#if (OPTIX_VERSION != EXPECTED_OPTIX_VERSION)
#error "Invalid OptiX header version included"
#endif

#include <optix_function_table_definition.h>

namespace rhi::cuda::optix::VERSION_TAG {

inline bool isOptixError(OptixResult result)
{
    return result != OPTIX_SUCCESS;
}

void reportOptixError(OptixResult result, const char* call, const char* file, int line, DeviceAdapter device)
{
    if (!device)
        return;

    char buf[4096];
    snprintf(
        buf,
        sizeof(buf),
        "%s failed: %s (%s)\nAt %s:%d\n",
        call,
        optixGetErrorName(result),
        optixGetErrorName(result),
        file,
        line
    );
    buf[sizeof(buf) - 1] = 0; // Ensure null termination
    device->handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, buf);
}

void reportOptixAssert(OptixResult result, const char* call, const char* file, int line)
{
    std::fprintf(
        stderr,
        "%s:%d: %s failed: %s (%s)\n",
        file,
        line,
        call,
        optixGetErrorString(result),
        optixGetErrorName(result)
    );
}

#define SLANG_OPTIX_RETURN_ON_FAIL(x)                                                                                  \
    {                                                                                                                  \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::optix::VERSION_TAG::isOptixError(_res))                                                       \
        {                                                                                                              \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

#define SLANG_OPTIX_RETURN_ON_FAIL_REPORT(x, device)                                                                   \
    {                                                                                                                  \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::optix::VERSION_TAG::isOptixError(_res))                                                       \
        {                                                                                                              \
            ::rhi::cuda::optix::VERSION_TAG::reportOptixError(_res, #x, __FILE__, __LINE__, device);                   \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

#define SLANG_OPTIX_ASSERT_ON_FAIL(x)                                                                                  \
    {                                                                                                                  \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::optix::VERSION_TAG::isOptixError(_res))                                                       \
        {                                                                                                              \
            ::rhi::cuda::optix::VERSION_TAG::reportOptixAssert(_res, #x, __FILE__, __LINE__);                          \
            SLANG_RHI_ASSERT_FAILURE("OptiX call failed");                                                             \
        }                                                                                                              \
    }

#if OPTIX_VERSION >= 90000

inline OptixCoopVecElemType translateCoopVecElemType(CooperativeVectorComponentType type)
{
    switch (type)
    {
    case CooperativeVectorComponentType::Float16:
        return OPTIX_COOP_VEC_ELEM_TYPE_FLOAT16;
    case CooperativeVectorComponentType::Float32:
        return OPTIX_COOP_VEC_ELEM_TYPE_FLOAT32;
    case CooperativeVectorComponentType::Sint8:
        return OPTIX_COOP_VEC_ELEM_TYPE_INT8;
    case CooperativeVectorComponentType::Sint32:
        return OPTIX_COOP_VEC_ELEM_TYPE_INT32;
    case CooperativeVectorComponentType::Uint8:
        return OPTIX_COOP_VEC_ELEM_TYPE_UINT8;
    case CooperativeVectorComponentType::Uint32:
        return OPTIX_COOP_VEC_ELEM_TYPE_UINT32;
    case CooperativeVectorComponentType::FloatE4M3:
        return OPTIX_COOP_VEC_ELEM_TYPE_FLOAT8_E4M3;
    case CooperativeVectorComponentType::FloatE5M2:
        return OPTIX_COOP_VEC_ELEM_TYPE_FLOAT8_E5M2;
    case CooperativeVectorComponentType::Float64:
    case CooperativeVectorComponentType::Sint16:
    case CooperativeVectorComponentType::Sint64:
    case CooperativeVectorComponentType::Uint16:
    case CooperativeVectorComponentType::Uint64:
    case CooperativeVectorComponentType::Sint8Packed:
    case CooperativeVectorComponentType::Uint8Packed:
        return OPTIX_COOP_VEC_ELEM_TYPE_UNKNOWN;
    }
    return OPTIX_COOP_VEC_ELEM_TYPE_UNKNOWN;
}

inline OptixCoopVecMatrixLayout translateCoopVecMatrixLayout(CooperativeVectorMatrixLayout layout)
{
    switch (layout)
    {
    case CooperativeVectorMatrixLayout::RowMajor:
        return OPTIX_COOP_VEC_MATRIX_LAYOUT_ROW_MAJOR;
    case CooperativeVectorMatrixLayout::ColumnMajor:
        return OPTIX_COOP_VEC_MATRIX_LAYOUT_COLUMN_MAJOR;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
        return OPTIX_COOP_VEC_MATRIX_LAYOUT_INFERENCING_OPTIMAL;
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        return OPTIX_COOP_VEC_MATRIX_LAYOUT_TRAINING_OPTIMAL;
    }
    return OPTIX_COOP_VEC_MATRIX_LAYOUT_ROW_MAJOR;
}

#endif

struct AccelerationStructureBuildDescConverter
{
public:
    stable_vector<CUdeviceptr> pointerList;
    stable_vector<unsigned int> flagList;
    std::vector<OptixBuildInput> buildInputs;
    OptixAccelBuildOptions buildOptions;

    Result convert(const AccelerationStructureBuildDesc& buildDesc, IDebugCallback* debugCallback);

private:
    unsigned int translateBuildFlags(AccelerationStructureBuildFlags flags) const;
    unsigned int translateGeometryFlags(AccelerationStructureGeometryFlags flags) const;
    OptixVertexFormat translateVertexFormat(Format format) const;
};

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
            if (spheres.vertexPositionFormat != Format::RGB32Float)
            {
                return SLANG_E_INVALID_ARG;
            }
            if (spheres.vertexRadiusFormat != Format::R32Float)
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
        for (uint32_t i = 0; i < buildDesc.inputCount; ++i)
        {
            const AccelerationStructureBuildInputLinearSweptSpheres& linearSweptSpheres =
                buildDesc.inputs[i].linearSweptSpheres;
            if (linearSweptSpheres.vertexBufferCount != 1)
            {
                return SLANG_E_INVALID_ARG;
            }
            if (linearSweptSpheres.vertexPositionFormat != Format::RGB32Float)
            {
                return SLANG_E_INVALID_ARG;
            }
            if (linearSweptSpheres.vertexRadiusFormat != Format::R32Float)
            {
                return SLANG_E_INVALID_ARG;
            }
            if (!linearSweptSpheres.indexBuffer)
            {
                return SLANG_E_INVALID_ARG;
            }
            if (linearSweptSpheres.endCapsMode == LinearSweptSpheresEndCapsMode::None)
            {
                return SLANG_E_INVALID_ARG;
            }
            if (linearSweptSpheres.indexingMode != LinearSweptSpheresIndexingMode::Successive)
            {
                return SLANG_E_INVALID_ARG;
            }

            OptixBuildInput& buildInput = buildInputs[i];
            buildInput = {};
            buildInput.type = OPTIX_BUILD_INPUT_TYPE_CURVES;
            buildInput.curveArray.curveType = OPTIX_PRIMITIVE_TYPE_ROUND_LINEAR;
            buildInput.curveArray.numPrimitives = linearSweptSpheres.primitiveCount;

            pointerList.push_back(linearSweptSpheres.vertexPositionBuffers[0].getDeviceAddress());
            buildInput.curveArray.numVertices = linearSweptSpheres.vertexCount;
            buildInput.curveArray.vertexBuffers = &pointerList.back();
            buildInput.curveArray.vertexStrideInBytes = linearSweptSpheres.vertexPositionStride;

            pointerList.push_back(linearSweptSpheres.vertexRadiusBuffers[0].getDeviceAddress());
            buildInput.curveArray.widthBuffers = &pointerList.back();
            buildInput.curveArray.widthStrideInBytes = linearSweptSpheres.vertexRadiusStride;

            buildInput.curveArray.indexBuffer = linearSweptSpheres.indexBuffer.getDeviceAddress();

            buildInput.curveArray.flag = translateGeometryFlags(linearSweptSpheres.flags);
        }
    }
    break;
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

unsigned int AccelerationStructureBuildDescConverter::translateGeometryFlags(
    AccelerationStructureGeometryFlags flags
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
    case Format::RGB32Float:
        return OPTIX_VERTEX_FORMAT_FLOAT3;
    case Format::RG32Float:
        return OPTIX_VERTEX_FORMAT_FLOAT2;
    case Format::RG16Float:
        return OPTIX_VERTEX_FORMAT_HALF2;
    default:
        return OPTIX_VERTEX_FORMAT_NONE;
    }
}

class PipelineImpl : public Pipeline
{
public:
    std::vector<OptixModule> m_modules;
    std::vector<OptixProgramGroup> m_programGroups;
    std::map<std::string, uint32_t> m_shaderGroupNameToIndex;
    OptixPipeline m_pipeline = nullptr;

    virtual ~PipelineImpl() override
    {
        if (m_pipeline)
            SLANG_OPTIX_ASSERT_ON_FAIL(optixPipelineDestroy(m_pipeline));
        for (OptixProgramGroup programGroup : m_programGroups)
            SLANG_OPTIX_ASSERT_ON_FAIL(optixProgramGroupDestroy(programGroup));
        for (OptixModule module : m_modules)
            SLANG_OPTIX_ASSERT_ON_FAIL(optixModuleDestroy(module));
    }

    virtual uint64_t getNativeHandle() const override { return reinterpret_cast<uint64_t>(m_pipeline); }
};

struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) SbtRecord
{
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

class ShaderBindingTableImpl : public ShaderBindingTable
{
public:
    CUdeviceptr m_buffer;
    OptixShaderBindingTable m_sbt;
    size_t m_raygenRecordSize;

    ~ShaderBindingTableImpl() { SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(m_buffer)); }
};

class ContextImpl : public Context
{
public:
    DeviceImpl* m_device;
    OptixDeviceContext m_deviceContext;
    bool m_ownsDeviceContext = false;

    virtual uint32_t getOptixVersion() const override { return OPTIX_VERSION; }

    virtual ~ContextImpl() override
    {
        if (m_ownsDeviceContext)
        {
            optixDeviceContextDestroy(m_deviceContext);
        }
    }

    virtual void* getOptixDeviceContext() const override { return m_deviceContext; }

    virtual Result createPipeline(
        const RayTracingPipelineDesc& desc,
        ShaderCompilationReporter* shaderCompilationReporter,
        Pipeline** outPipeline
    ) override
    {
        TimePoint startTime = Timer::now();

        ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
        SLANG_RHI_ASSERT(!program->m_modules.empty());

        OptixPipelineCompileOptions optixPipelineCompileOptions = {};
        optixPipelineCompileOptions.usesMotionBlur = 0;
        optixPipelineCompileOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
        optixPipelineCompileOptions.numPayloadValues =
            (desc.maxRayPayloadSize + sizeof(uint32_t) - 1) / sizeof(uint32_t);
        optixPipelineCompileOptions.numAttributeValues =
            (desc.maxAttributeSizeInBytes + sizeof(uint32_t) - 1) / sizeof(uint32_t);
        optixPipelineCompileOptions.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;

        // Check if SLANG_globalParams exists in any module.
        // The Slang compiler only generates this variable when there are uniform parameters.
        // If no uniform parameters exist, we set this to nullptr to avoid OptiX validation warnings.
        bool hasGlobalParams = false;
        for (const auto& module : program->m_modules)
        {
            const char* ptxCode = static_cast<const char*>(module.code->getBufferPointer());
            if (std::strstr(ptxCode, "SLANG_globalParams"))
            {
                hasGlobalParams = true;
                break;
            }
        }
        optixPipelineCompileOptions.pipelineLaunchParamsVariableName = hasGlobalParams ? "SLANG_globalParams" : nullptr;

        optixPipelineCompileOptions.usesPrimitiveTypeFlags = 0;
        if (is_set(desc.flags, RayTracingPipelineFlags::EnableSpheres))
            optixPipelineCompileOptions.usesPrimitiveTypeFlags |= OPTIX_PRIMITIVE_TYPE_FLAGS_SPHERE;
        if (is_set(desc.flags, RayTracingPipelineFlags::EnableLinearSweptSpheres))
            optixPipelineCompileOptions.usesPrimitiveTypeFlags |= OPTIX_PRIMITIVE_TYPE_FLAGS_ROUND_LINEAR;

        optixPipelineCompileOptions.allowOpacityMicromaps = 0;

        OptixModuleCompileOptions optixModuleCompileOptions = {};
        optixModuleCompileOptions.maxRegisterCount = 0; // no limit
        optixModuleCompileOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
        optixModuleCompileOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_DEFAULT;
        optixModuleCompileOptions.boundValues = nullptr;
        optixModuleCompileOptions.numBoundValues = 0;
        optixModuleCompileOptions.numPayloadTypes = 0;
        optixModuleCompileOptions.payloadTypes = nullptr;

        OptixProgramGroupOptions optixProgramGroupOptions = {};

        // Create optix modules & program groups
        std::vector<OptixModule> optixModules;
        std::map<std::string, uint32_t> entryPointNameToModuleIndex;
        std::vector<OptixProgramGroup> optixProgramGroups;
        std::map<std::string, uint32_t> shaderGroupNameToIndex;
        for (const auto& module : program->m_modules)
        {
            SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
                optixModuleCreate(
                    m_deviceContext,
                    &optixModuleCompileOptions,
                    &optixPipelineCompileOptions,
                    static_cast<const char*>(module.code->getBufferPointer()),
                    module.code->getBufferSize(),
                    nullptr,
                    0,
                    &optixModules.emplace_back()
                ),
                m_device
            );
            entryPointNameToModuleIndex[module.entryPointName] = optixModules.size() - 1;

            OptixProgramGroupDesc optixProgramGroupDesc = {};
            std::string entryFunctionName;
            switch (module.stage)
            {
            case SLANG_STAGE_RAY_GENERATION:
                optixProgramGroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
                optixProgramGroupDesc.raygen.module = optixModules.back();
                entryFunctionName = "__raygen__" + module.entryPointName;
                optixProgramGroupDesc.raygen.entryFunctionName = entryFunctionName.data();
                break;
            case SLANG_STAGE_MISS:
                optixProgramGroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
                optixProgramGroupDesc.miss.module = optixModules.back();
                entryFunctionName = "__miss__" + module.entryPointName;
                optixProgramGroupDesc.miss.entryFunctionName = entryFunctionName.data();
                break;
            case SLANG_STAGE_CALLABLE:
                optixProgramGroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
                // TODO: support continuation callables
                optixProgramGroupDesc.callables.moduleDC = optixModules.back();
                entryFunctionName = "__callable__" + module.entryPointName;
                optixProgramGroupDesc.callables.entryFunctionNameDC = entryFunctionName.data();
                break;
            default:
                continue;
            }
            SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
                optixProgramGroupCreate(
                    m_deviceContext,
                    &optixProgramGroupDesc,
                    1,
                    &optixProgramGroupOptions,
                    nullptr,
                    0,
                    &optixProgramGroups.emplace_back()
                ),
                m_device
            );
            shaderGroupNameToIndex[module.entryPointName] = optixProgramGroups.size() - 1;
        }

        // If we're using spheres, hit groups may use the builtin sphere intersector.
        OptixModule builtinISModuleSphere = nullptr;
        if (is_set(desc.flags, RayTracingPipelineFlags::EnableSpheres))
        {
            OptixBuiltinISOptions builtinISOptions = {};
            builtinISOptions.builtinISModuleType = OPTIX_PRIMITIVE_TYPE_SPHERE;
            SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
                optixBuiltinISModuleGet(
                    m_deviceContext,
                    &optixModuleCompileOptions,
                    &optixPipelineCompileOptions,
                    &builtinISOptions,
                    &builtinISModuleSphere
                ),
                m_device
            );
        }

        // If we're using linear swept spheres, hit groups may use the builtin linear swept sphere intersector.
        OptixModule builtinISModuleLinearSweptSpheres = nullptr;
        if (is_set(desc.flags, RayTracingPipelineFlags::EnableLinearSweptSpheres))
        {
            OptixBuiltinISOptions builtinISOptions = {};
            builtinISOptions.builtinISModuleType = OPTIX_PRIMITIVE_TYPE_ROUND_LINEAR;
            SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
                optixBuiltinISModuleGet(
                    m_deviceContext,
                    &optixModuleCompileOptions,
                    &optixPipelineCompileOptions,
                    &builtinISOptions,
                    &builtinISModuleLinearSweptSpheres
                ),
                m_device
            );
        }

        // Create program groups for hit groups
        for (uint32_t hitGroupIndex = 0; hitGroupIndex < desc.hitGroupCount; ++hitGroupIndex)
        {
            const HitGroupDesc& hitGroupDesc = desc.hitGroups[hitGroupIndex];

            OptixProgramGroupDesc optixProgramGroupDesc = {};
            optixProgramGroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
            std::string entryFunctionNameCH;
            std::string entryFunctionNameAH;
            std::string entryFunctionNameIS;
            if (hitGroupDesc.closestHitEntryPoint)
            {
                optixProgramGroupDesc.hitgroup.moduleCH =
                    optixModules[entryPointNameToModuleIndex[hitGroupDesc.closestHitEntryPoint]];
                entryFunctionNameCH = std::string("__closesthit__") + hitGroupDesc.closestHitEntryPoint;
                optixProgramGroupDesc.hitgroup.entryFunctionNameCH = entryFunctionNameCH.data();
            }
            if (hitGroupDesc.anyHitEntryPoint)
            {
                optixProgramGroupDesc.hitgroup.moduleAH =
                    optixModules[entryPointNameToModuleIndex[hitGroupDesc.anyHitEntryPoint]];
                entryFunctionNameAH = std::string("__anyhit__") + hitGroupDesc.anyHitEntryPoint;
                optixProgramGroupDesc.hitgroup.entryFunctionNameAH = entryFunctionNameAH.data();
            }
            if (hitGroupDesc.intersectionEntryPoint)
            {
                if (std::strcmp(hitGroupDesc.intersectionEntryPoint, "__builtin_intersection__sphere") == 0)
                    optixProgramGroupDesc.hitgroup.moduleIS = builtinISModuleSphere;
                else if (std::strcmp(
                             hitGroupDesc.intersectionEntryPoint,
                             "__builtin_intersection__linear_swept_spheres"
                         ) == 0)
                    optixProgramGroupDesc.hitgroup.moduleIS = builtinISModuleLinearSweptSpheres;
                else
                {
                    optixProgramGroupDesc.hitgroup.moduleIS =
                        optixModules[entryPointNameToModuleIndex[hitGroupDesc.intersectionEntryPoint]];
                    entryFunctionNameIS = std::string("__intersection__") + hitGroupDesc.intersectionEntryPoint;
                    optixProgramGroupDesc.hitgroup.entryFunctionNameIS = entryFunctionNameIS.data();
                }
            }

            SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
                optixProgramGroupCreate(
                    m_deviceContext,
                    &optixProgramGroupDesc,
                    1,
                    &optixProgramGroupOptions,
                    nullptr,
                    0,
                    &optixProgramGroups.emplace_back()
                ),
                m_device
            );
            shaderGroupNameToIndex[hitGroupDesc.hitGroupName] = optixProgramGroups.size() - 1;
        }

        OptixPipeline optixPipeline = nullptr;
        OptixPipelineLinkOptions optixPipelineLinkOptions = {};
        optixPipelineLinkOptions.maxTraceDepth = desc.maxRecursion;

        SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
            optixPipelineCreate(
                m_deviceContext,
                &optixPipelineCompileOptions,
                &optixPipelineLinkOptions,
                optixProgramGroups.data(),
                optixProgramGroups.size(),
                nullptr,
                0,
                &optixPipeline
            ),
            m_device
        );

        // Report the pipeline creation time.
        if (shaderCompilationReporter)
        {
            shaderCompilationReporter->reportCreatePipeline(
                program,
                ShaderCompilationReporter::PipelineType::RayTracing,
                startTime,
                Timer::now(),
                false,
                0
            );
        }

        RefPtr<PipelineImpl> pipeline = new PipelineImpl();
        pipeline->m_modules = std::move(optixModules);
        pipeline->m_programGroups = std::move(optixProgramGroups);
        pipeline->m_shaderGroupNameToIndex = std::move(shaderGroupNameToIndex);
        pipeline->m_pipeline = optixPipeline;
        returnRefPtr(outPipeline, pipeline);
        return SLANG_OK;
    }

    virtual Result createShaderBindingTable(
        ShaderTableImpl* shaderTable,
        Pipeline* pipeline,
        ShaderBindingTable** outShaderBindingTable
    ) override
    {
        PipelineImpl* pipelineImpl = checked_cast<PipelineImpl*>(pipeline);

        RefPtr<ShaderBindingTableImpl> shaderBindingTable = new ShaderBindingTableImpl();

        shaderBindingTable->m_raygenRecordSize = sizeof(SbtRecord);

        size_t tableSize = (shaderTable->m_rayGenShaderCount + shaderTable->m_missShaderCount +
                            shaderTable->m_hitGroupCount + shaderTable->m_callableShaderCount) *
                           sizeof(SbtRecord);

        auto hostBuffer = std::make_unique<uint8_t[]>(tableSize);
        std::memset(hostBuffer.get(), 0, tableSize);
        auto hostPtr = hostBuffer.get();

        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&shaderBindingTable->m_buffer, tableSize), m_device);
        CUdeviceptr deviceBuffer = shaderBindingTable->m_buffer;
        CUdeviceptr devicePtr = deviceBuffer;

        OptixShaderBindingTable& sbt = shaderBindingTable->m_sbt;
        const std::vector<std::string>& shaderGroupNames = shaderTable->m_shaderGroupNames;
        const std::map<std::string, uint32_t>& shaderGroupNameToIndex = pipelineImpl->m_shaderGroupNameToIndex;

        size_t shaderTableEntryIndex = 0;

        if (shaderTable->m_rayGenShaderCount > 0)
        {
            sbt.raygenRecord = devicePtr;
            for (uint32_t i = 0; i < shaderTable->m_rayGenShaderCount; i++)
            {
                auto it = shaderGroupNameToIndex.find(shaderGroupNames[shaderTableEntryIndex++]);
                if (it == shaderGroupNameToIndex.end())
                    continue;
                SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
                    optixSbtRecordPackHeader(pipelineImpl->m_programGroups[it->second], hostPtr),
                    m_device
                );
                hostPtr += sizeof(SbtRecord);
                devicePtr += sizeof(SbtRecord);
            }
        }

        if (shaderTable->m_missShaderCount > 0)
        {
            sbt.missRecordBase = devicePtr;
            sbt.missRecordStrideInBytes = sizeof(SbtRecord);
            sbt.missRecordCount = shaderTable->m_missShaderCount;
            for (uint32_t i = 0; i < shaderTable->m_missShaderCount; i++)
            {
                auto it = shaderGroupNameToIndex.find(shaderGroupNames[shaderTableEntryIndex++]);
                if (it == shaderGroupNameToIndex.end())
                    continue;
                SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
                    optixSbtRecordPackHeader(pipelineImpl->m_programGroups[it->second], hostPtr),
                    m_device
                );
                hostPtr += sizeof(SbtRecord);
                devicePtr += sizeof(SbtRecord);
            }
        }

        if (shaderTable->m_hitGroupCount > 0)
        {
            sbt.hitgroupRecordBase = devicePtr;
            sbt.hitgroupRecordStrideInBytes = sizeof(SbtRecord);
            sbt.hitgroupRecordCount = shaderTable->m_hitGroupCount;
            for (uint32_t i = 0; i < shaderTable->m_hitGroupCount; i++)
            {
                auto it = shaderGroupNameToIndex.find(shaderGroupNames[shaderTableEntryIndex++]);
                if (it == shaderGroupNameToIndex.end())
                    continue;
                SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
                    optixSbtRecordPackHeader(pipelineImpl->m_programGroups[it->second], hostPtr),
                    m_device
                );
                hostPtr += sizeof(SbtRecord);
                devicePtr += sizeof(SbtRecord);
            }
        }

        if (shaderTable->m_callableShaderCount > 0)
        {
            sbt.callablesRecordBase = devicePtr;
            sbt.callablesRecordStrideInBytes = sizeof(SbtRecord);
            sbt.callablesRecordCount = shaderTable->m_callableShaderCount;
            for (uint32_t i = 0; i < shaderTable->m_callableShaderCount; i++)
            {
                auto it = shaderGroupNameToIndex.find(shaderGroupNames[shaderTableEntryIndex++]);
                if (it == shaderGroupNameToIndex.end())
                    continue;
                SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
                    optixSbtRecordPackHeader(pipelineImpl->m_programGroups[it->second], hostPtr),
                    m_device
                );
                hostPtr += sizeof(SbtRecord);
                devicePtr += sizeof(SbtRecord);
            }
        }

        SLANG_CUDA_ASSERT_ON_FAIL(cuMemcpyHtoD(deviceBuffer, hostBuffer.get(), tableSize));

        returnRefPtr(outShaderBindingTable, shaderBindingTable);
        return SLANG_OK;
    }

    virtual Result getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) override
    {
        AccelerationStructureBuildDescConverter converter;
        SLANG_RETURN_ON_FAIL(converter.convert(desc, m_device->m_debugCallback));
        OptixAccelBufferSizes sizes;
        SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
            optixAccelComputeMemoryUsage(
                m_deviceContext,
                &converter.buildOptions,
                converter.buildInputs.data(),
                converter.buildInputs.size(),
                &sizes
            ),
            m_device
        );
        outSizes->accelerationStructureSize = sizes.outputSizeInBytes;
        outSizes->scratchSize = sizes.tempSizeInBytes;
        outSizes->updateScratchSize = sizes.tempUpdateSizeInBytes;
        return SLANG_OK;
    }

    virtual void buildAccelerationStructure(
        CUstream stream,
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureImpl* dst,
        AccelerationStructureImpl* src,
        BufferOffsetPair scratchBuffer,
        uint32_t propertyQueryCount,
        const AccelerationStructureQueryDesc* queryDescs
    ) override
    {
        AccelerationStructureBuildDescConverter converter;
        if (converter.convert(desc, m_device->m_debugCallback) != SLANG_OK)
            return;

        short_vector<OptixAccelEmitDesc, 8> emittedProperties;
        for (uint32_t i = 0; i < propertyQueryCount; i++)
        {
            if (queryDescs[i].queryType == QueryType::AccelerationStructureCompactedSize)
            {
                PlainBufferProxyQueryPoolImpl* queryPool =
                    checked_cast<PlainBufferProxyQueryPoolImpl*>(queryDescs[i].queryPool);
                OptixAccelEmitDesc property = {};
                property.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
                property.result = queryPool->m_buffer + queryDescs[i].firstQueryIndex * sizeof(uint64_t);
                emittedProperties.push_back(property);
            }
        }

        SLANG_OPTIX_ASSERT_ON_FAIL(optixAccelBuild(
            m_deviceContext,
            stream,
            &converter.buildOptions,
            converter.buildInputs.data(),
            converter.buildInputs.size(),
            scratchBuffer.getDeviceAddress(),
            checked_cast<BufferImpl*>(scratchBuffer.buffer)->m_desc.size - scratchBuffer.offset,
            dst->m_buffer,
            dst->m_desc.size,
            &dst->m_handle,
            emittedProperties.empty() ? nullptr : emittedProperties.data(),
            emittedProperties.size()
        ));
    }

    virtual void copyAccelerationStructure(
        CUstream stream,
        AccelerationStructureImpl* dst,
        AccelerationStructureImpl* src,
        AccelerationStructureCopyMode mode
    ) override
    {
        switch (mode)
        {
        case AccelerationStructureCopyMode::Clone:
        {
#if 0
                OptixRelocationInfo relocInfo = {};
                optixAccelGetRelocationInfo(m_commandBuffer->m_device->m_ctx.optixContext, src->m_handle, &relocInfo);

                // TODO setup inputs
                OptixRelocateInput relocInput = {};

                cuMemcpyDtoD(dst->m_buffer, src->m_buffer, src->m_desc.size);

                optixAccelRelocate(
                    m_commandBuffer->m_device->m_ctx.optixContext,
                    m_stream,
                    &relocInfo,
                    &relocInput,
                    1,
                    dst->m_buffer,
                    dst->m_desc.size,
                    &dst->m_handle
                );
                break;
#endif
        }
        case AccelerationStructureCopyMode::Compact:
            SLANG_OPTIX_ASSERT_ON_FAIL(optixAccelCompact(
                m_deviceContext,
                stream,
                src->m_handle,
                dst->m_buffer,
                dst->m_desc.size,
                &dst->m_handle
            ));
            break;
        }
    }

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
    ) override
    {
        PipelineImpl* pipelineImpl = checked_cast<PipelineImpl*>(pipeline);
        ShaderBindingTableImpl* shaderBindingTableImpl = checked_cast<ShaderBindingTableImpl*>(shaderBindingTable);
        OptixShaderBindingTable sbt = shaderBindingTableImpl->m_sbt;
        sbt.raygenRecord += rayGenShaderIndex * shaderBindingTableImpl->m_raygenRecordSize;

        SLANG_OPTIX_ASSERT_ON_FAIL(optixLaunch(
            pipelineImpl->m_pipeline,
            stream,
            pipelineParams,
            pipelineParamsSize,
            &sbt,
            width,
            height,
            depth
        ));
    }

    virtual bool getCooperativeVectorSupport() const override
    {
#if OPTIX_VERSION >= 90000
        unsigned int coopVecSupport = 0;
        if (optixDeviceContextGetProperty(
                m_deviceContext,
                OPTIX_DEVICE_PROPERTY_COOP_VEC,
                &coopVecSupport,
                sizeof(coopVecSupport)
            ) == OPTIX_SUCCESS)
        {
            return coopVecSupport & OPTIX_DEVICE_PROPERTY_COOP_VEC_FLAG_STANDARD;
        }
#endif
        return false;
    }

    virtual Result getCooperativeVectorMatrixSize(
        uint32_t rowCount,
        uint32_t colCount,
        CooperativeVectorComponentType componentType,
        CooperativeVectorMatrixLayout layout,
        size_t rowColumnStride,
        size_t* outSize
    ) const override
    {
#if OPTIX_VERSION >= 90000
        SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
            optixCoopVecMatrixComputeSize(
                m_deviceContext,
                rowCount,
                colCount,
                translateCoopVecElemType(componentType),
                translateCoopVecMatrixLayout(layout),
                rowColumnStride,
                outSize
            ),
            m_device
        );
        return SLANG_OK;
#endif
        return SLANG_E_NOT_AVAILABLE;
    }

    virtual Result convertCooperativeVectorMatrix(
        CUstream stream,
        CUdeviceptr dstBuffer,
        const CooperativeVectorMatrixDesc* dstDescs,
        CUdeviceptr srcBuffer,
        const CooperativeVectorMatrixDesc* srcDescs,
        uint32_t matrixCount
    ) const override
    {
#if OPTIX_VERSION >= 90000
        short_vector<OptixCoopVecMatrixDescription> optixDstLayers;
        short_vector<OptixCoopVecMatrixDescription> optixSrcLayers;
        for (uint32_t i = 0; i < matrixCount; ++i)
        {
            const CooperativeVectorMatrixDesc& dstDesc = dstDescs[i];
            OptixCoopVecMatrixDescription optixDstLayer = {};
            optixDstLayer.N = dstDesc.rowCount;
            optixDstLayer.K = dstDesc.colCount;
            optixDstLayer.offsetInBytes = dstDesc.offset;
            optixDstLayer.elementType = translateCoopVecElemType(dstDesc.componentType);
            optixDstLayer.layout = translateCoopVecMatrixLayout(dstDesc.layout);
            optixDstLayer.rowColumnStrideInBytes = dstDesc.rowColumnStride;
            optixDstLayer.sizeInBytes = dstDesc.size;
            optixDstLayers.push_back(optixDstLayer);

            const CooperativeVectorMatrixDesc& srcDesc = srcDescs[i];
            OptixCoopVecMatrixDescription optixSrcLayer = {};
            optixSrcLayer.N = srcDesc.rowCount;
            optixSrcLayer.K = srcDesc.colCount;
            optixSrcLayer.offsetInBytes = srcDesc.offset;
            optixSrcLayer.elementType = translateCoopVecElemType(srcDesc.componentType);
            optixSrcLayer.layout = translateCoopVecMatrixLayout(srcDesc.layout);
            optixSrcLayer.rowColumnStrideInBytes = srcDesc.rowColumnStride;
            optixSrcLayer.sizeInBytes = srcDesc.size;
            optixSrcLayers.push_back(optixSrcLayer);
        }

        OptixNetworkDescription optixDstNetwork = {};
        optixDstNetwork.layers = optixDstLayers.data();
        optixDstNetwork.numLayers = matrixCount;

        OptixNetworkDescription optixSrcNetwork = {};
        optixSrcNetwork.layers = optixSrcLayers.data();
        optixSrcNetwork.numLayers = matrixCount;

        SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
            optixCoopVecMatrixConvert(
                m_deviceContext,
                stream,
                1,
                &optixSrcNetwork,
                srcBuffer,
                0,
                &optixDstNetwork,
                dstBuffer,
                0
            ),
            m_device
        );

        return SLANG_OK;
#endif
        return SLANG_E_NOT_AVAILABLE;
    }
};

Result createContext(const ContextDesc& desc, Context** outContext)
{
    RefPtr<ContextImpl> context = new ContextImpl();
    context->m_device = desc.device;

    if (desc.existingOptixDeviceContext)
    {
        context->m_deviceContext = static_cast<OptixDeviceContext>(desc.existingOptixDeviceContext);
        context->m_ownsDeviceContext = false;
    }
    else
    {
        static auto logCallback = [](unsigned int level, const char* tag, const char* message, void* userData)
        {
            ContextImpl* context_ = static_cast<ContextImpl*>(userData);
            DebugMessageType type;
            switch (level)
            {
            case 1: // fatal
                type = DebugMessageType::Error;
                break;
            case 2: // error
                type = DebugMessageType::Error;
                break;
            case 3: // warning
                type = DebugMessageType::Warning;
                break;
            case 4: // print
                type = DebugMessageType::Info;
                break;
            default:
                return;
            }

            char msg[4096];
            int msgSize = snprintf(msg, sizeof(msg), "[%s]: %s", tag, message);
            if (msgSize < 0)
                return;
            else if (msgSize >= int(sizeof(msg)))
                msg[sizeof(msg) - 1] = 0;

            context_->m_device->handleMessage(type, DebugMessageSource::Driver, msg);
        };

        OptixDeviceContextOptions options = {};
        options.logCallbackFunction = logCallback;
        options.logCallbackLevel = 4;
        options.logCallbackData = context;
        options.validationMode = desc.enableRayTracingValidation ? OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_ALL
                                                                 : OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_OFF;

        SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
            optixDeviceContextCreate(desc.device->m_ctx.context, &options, &context->m_deviceContext),
            desc.device
        );
        context->m_ownsDeviceContext = true;
    }

    returnRefPtr(outContext, context);
    return SLANG_OK;
}

uint32_t optixVersion = OPTIX_VERSION;

bool initialize(IDebugCallback* debugCallback)
{
    OptixResult result = optixInit();
    if (result != OPTIX_SUCCESS)
    {
        if (debugCallback)
        {
            std::string msg = string::format(
                "Failed to initialize OptiX %d.%d: %s (%s)",
                OPTIX_VERSION / 10000,
                (OPTIX_VERSION % 10000) / 100,
                optixGetErrorString(result),
                optixGetErrorName(result)
            );
            debugCallback->handleMessage(DebugMessageType::Warning, DebugMessageSource::Layer, msg.data());
        }
    }
    return result == OPTIX_SUCCESS;
}

} // namespace rhi::cuda::optix::VERSION_TAG

// Denoiser API

namespace rhi::optix_denoiser::VERSION_TAG {

// To ensure our enums and structs match OptiX's, we use static_asserts.

/// Check enum value
#define CHECK_ENUM(x) static_assert((int)x == (int)::x)
/// Check struct size (this does not fully check struct field offsets, but is a good proxy)
#define CHECK_STRUCT(x) static_assert(sizeof(x) == sizeof(::x))

// OptixResult
CHECK_ENUM(OPTIX_SUCCESS);
CHECK_ENUM(OPTIX_ERROR_INVALID_VALUE);
CHECK_ENUM(OPTIX_ERROR_HOST_OUT_OF_MEMORY);
CHECK_ENUM(OPTIX_ERROR_INVALID_OPERATION);
CHECK_ENUM(OPTIX_ERROR_FILE_IO_ERROR);
CHECK_ENUM(OPTIX_ERROR_INVALID_FILE_FORMAT);
CHECK_ENUM(OPTIX_ERROR_DISK_CACHE_INVALID_PATH);
CHECK_ENUM(OPTIX_ERROR_DISK_CACHE_PERMISSION_ERROR);
CHECK_ENUM(OPTIX_ERROR_DISK_CACHE_DATABASE_ERROR);
CHECK_ENUM(OPTIX_ERROR_DISK_CACHE_INVALID_DATA);
CHECK_ENUM(OPTIX_ERROR_LAUNCH_FAILURE);
CHECK_ENUM(OPTIX_ERROR_INVALID_DEVICE_CONTEXT);
CHECK_ENUM(OPTIX_ERROR_CUDA_NOT_INITIALIZED);
CHECK_ENUM(OPTIX_ERROR_VALIDATION_FAILURE);
CHECK_ENUM(OPTIX_ERROR_INVALID_INPUT);
CHECK_ENUM(OPTIX_ERROR_INVALID_LAUNCH_PARAMETER);
CHECK_ENUM(OPTIX_ERROR_INVALID_PAYLOAD_ACCESS);
CHECK_ENUM(OPTIX_ERROR_INVALID_ATTRIBUTE_ACCESS);
CHECK_ENUM(OPTIX_ERROR_INVALID_FUNCTION_USE);
CHECK_ENUM(OPTIX_ERROR_INVALID_FUNCTION_ARGUMENTS);
CHECK_ENUM(OPTIX_ERROR_PIPELINE_OUT_OF_CONSTANT_MEMORY);
CHECK_ENUM(OPTIX_ERROR_PIPELINE_LINK_ERROR);
CHECK_ENUM(OPTIX_ERROR_ILLEGAL_DURING_TASK_EXECUTE);
CHECK_ENUM(OPTIX_ERROR_INTERNAL_COMPILER_ERROR);
CHECK_ENUM(OPTIX_ERROR_DENOISER_MODEL_NOT_SET);
CHECK_ENUM(OPTIX_ERROR_DENOISER_NOT_INITIALIZED);
CHECK_ENUM(OPTIX_ERROR_NOT_COMPATIBLE);
CHECK_ENUM(OPTIX_ERROR_PAYLOAD_TYPE_MISMATCH);
CHECK_ENUM(OPTIX_ERROR_PAYLOAD_TYPE_RESOLUTION_FAILED);
CHECK_ENUM(OPTIX_ERROR_PAYLOAD_TYPE_ID_INVALID);
CHECK_ENUM(OPTIX_ERROR_NOT_SUPPORTED);
CHECK_ENUM(OPTIX_ERROR_UNSUPPORTED_ABI_VERSION);
CHECK_ENUM(OPTIX_ERROR_FUNCTION_TABLE_SIZE_MISMATCH);
CHECK_ENUM(OPTIX_ERROR_INVALID_ENTRY_FUNCTION_OPTIONS);
CHECK_ENUM(OPTIX_ERROR_LIBRARY_NOT_FOUND);
CHECK_ENUM(OPTIX_ERROR_ENTRY_SYMBOL_NOT_FOUND);
CHECK_ENUM(OPTIX_ERROR_LIBRARY_UNLOAD_FAILURE);
CHECK_ENUM(OPTIX_ERROR_DEVICE_OUT_OF_MEMORY);
#if OPTIX_VERSION >= 90000
CHECK_ENUM(OPTIX_ERROR_INVALID_POINTER);
#endif
CHECK_ENUM(OPTIX_ERROR_CUDA_ERROR);
CHECK_ENUM(OPTIX_ERROR_INTERNAL_ERROR);
CHECK_ENUM(OPTIX_ERROR_UNKNOWN);

// OptixDeviceContextValidationMode
CHECK_ENUM(OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_OFF);
CHECK_ENUM(OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_ALL);

// OptixPixelFormat
CHECK_ENUM(OPTIX_PIXEL_FORMAT_HALF1);
CHECK_ENUM(OPTIX_PIXEL_FORMAT_HALF2);
CHECK_ENUM(OPTIX_PIXEL_FORMAT_HALF3);
CHECK_ENUM(OPTIX_PIXEL_FORMAT_HALF4);
CHECK_ENUM(OPTIX_PIXEL_FORMAT_FLOAT1);
CHECK_ENUM(OPTIX_PIXEL_FORMAT_FLOAT2);
CHECK_ENUM(OPTIX_PIXEL_FORMAT_FLOAT3);
CHECK_ENUM(OPTIX_PIXEL_FORMAT_FLOAT4);
CHECK_ENUM(OPTIX_PIXEL_FORMAT_UCHAR3);
CHECK_ENUM(OPTIX_PIXEL_FORMAT_UCHAR4);
CHECK_ENUM(OPTIX_PIXEL_FORMAT_INTERNAL_GUIDE_LAYER);

// OptixDenoiserModelKind
CHECK_ENUM(OPTIX_DENOISER_MODEL_KIND_AOV);
CHECK_ENUM(OPTIX_DENOISER_MODEL_KIND_TEMPORAL_AOV);
CHECK_ENUM(OPTIX_DENOISER_MODEL_KIND_UPSCALE2X);
CHECK_ENUM(OPTIX_DENOISER_MODEL_KIND_TEMPORAL_UPSCALE2X);
CHECK_ENUM(OPTIX_DENOISER_MODEL_KIND_LDR);
CHECK_ENUM(OPTIX_DENOISER_MODEL_KIND_HDR);
CHECK_ENUM(OPTIX_DENOISER_MODEL_KIND_TEMPORAL);

// OptixDenoiserAlphaMode
CHECK_ENUM(OPTIX_DENOISER_ALPHA_MODE_COPY);
CHECK_ENUM(OPTIX_DENOISER_ALPHA_MODE_DENOISE);

// OptixDenoiserAOVType
CHECK_ENUM(OPTIX_DENOISER_AOV_TYPE_NONE);
CHECK_ENUM(OPTIX_DENOISER_AOV_TYPE_BEAUTY);
CHECK_ENUM(OPTIX_DENOISER_AOV_TYPE_SPECULAR);
CHECK_ENUM(OPTIX_DENOISER_AOV_TYPE_REFLECTION);
CHECK_ENUM(OPTIX_DENOISER_AOV_TYPE_REFRACTION);
CHECK_ENUM(OPTIX_DENOISER_AOV_TYPE_DIFFUSE);

// Structs
CHECK_STRUCT(OptixDeviceContextOptions);
CHECK_STRUCT(OptixImage2D);
CHECK_STRUCT(OptixDenoiserOptions);
CHECK_STRUCT(OptixDenoiserGuideLayer);
CHECK_STRUCT(OptixDenoiserLayer);
CHECK_STRUCT(OptixDenoiserParams);
CHECK_STRUCT(OptixDenoiserSizes);

struct OptixDenoiserAPIImpl : public IOptixDenoiserAPI, public ComObject
{
    SLANG_COM_OBJECT_IUNKNOWN_ALL

    IOptixDenoiserAPI* getInterface(const Guid& guid)
    {
        if (guid == ISlangUnknown::getTypeGuid() || guid == IOptixDenoiserAPI::getTypeGuid())
            return static_cast<IOptixDenoiserAPI*>(this);
        return nullptr;
    }

    Result init()
    {
        if (!rhiCudaDriverApiInit())
        {
            return SLANG_FAIL;
        }
        return SLANG_OK;
    }

    virtual ~OptixDenoiserAPIImpl() override { rhiCudaDriverApiShutdown(); }

    virtual SLANG_NO_THROW const char* SLANG_MCALL optixGetErrorName(OptixResult result) override
    {
        return ::optixGetErrorName((::OptixResult)result);
    }

    virtual SLANG_NO_THROW const char* SLANG_MCALL optixGetErrorString(OptixResult result) override
    {
        return ::optixGetErrorString((::OptixResult)result);
    }

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDeviceContextCreate(
        CUcontext fromContext,
        const OptixDeviceContextOptions* options,
        OptixDeviceContext* context
    ) override
    {
        return (OptixResult)::optixDeviceContextCreate(
            fromContext,
            (const ::OptixDeviceContextOptions*)options,
            (::OptixDeviceContext*)context
        );
    }

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDeviceContextDestroy(OptixDeviceContext context) override
    {
        return (OptixResult)::optixDeviceContextDestroy((::OptixDeviceContext)context);
    }

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserCreate(
        OptixDeviceContext context,
        OptixDenoiserModelKind modelKind,
        const OptixDenoiserOptions* options,
        OptixDenoiser* returnHandle
    ) override
    {
        return (OptixResult)::optixDenoiserCreate(
            (::OptixDeviceContext)context,
            (::OptixDenoiserModelKind)modelKind,
            (const ::OptixDenoiserOptions*)options,
            (::OptixDenoiser*)returnHandle
        );
    }

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserCreateWithUserModel(
        OptixDeviceContext context,
        const void* data,
        size_t dataSizeInBytes,
        OptixDenoiser* returnHandle
    ) override
    {
        return (OptixResult)::optixDenoiserCreateWithUserModel(
            (::OptixDeviceContext)context,
            data,
            dataSizeInBytes,
            (::OptixDenoiser*)returnHandle
        );
    }

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserDestroy(OptixDenoiser handle) override
    {
        return (OptixResult)::optixDenoiserDestroy((::OptixDenoiser)handle);
    }

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserComputeMemoryResources(
        const OptixDenoiser handle,
        unsigned int maximumInputWidth,
        unsigned int maximumInputHeight,
        OptixDenoiserSizes* returnSizes
    ) override
    {
        return (OptixResult)::optixDenoiserComputeMemoryResources(
            (::OptixDenoiser)handle,
            maximumInputWidth,
            maximumInputHeight,
            (::OptixDenoiserSizes*)returnSizes
        );
    }

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserSetup(
        OptixDenoiser denoiser,
        CUstream stream,
        unsigned int inputWidth,
        unsigned int inputHeight,
        CUdeviceptr denoiserState,
        size_t denoiserStateSizeInBytes,
        CUdeviceptr scratch,
        size_t scratchSizeInBytes
    ) override
    {
        return (OptixResult)::optixDenoiserSetup(
            (::OptixDenoiser)denoiser,
            stream,
            inputWidth,
            inputHeight,
            denoiserState,
            denoiserStateSizeInBytes,
            scratch,
            scratchSizeInBytes
        );
    }

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserInvoke(
        OptixDenoiser handle,
        CUstream stream,
        const OptixDenoiserParams* params,
        CUdeviceptr denoiserData,
        size_t denoiserDataSize,
        const OptixDenoiserGuideLayer* guideLayer,
        const OptixDenoiserLayer* layers,
        unsigned int numLayers,
        unsigned int inputOffsetX,
        unsigned int inputOffsetY,
        CUdeviceptr scratch,
        size_t scratchSizeInBytes
    ) override
    {
        return (OptixResult)::optixDenoiserInvoke(
            (::OptixDenoiser)handle,
            stream,
            (const ::OptixDenoiserParams*)params,
            denoiserData,
            denoiserDataSize,
            (const ::OptixDenoiserGuideLayer*)guideLayer,
            (const ::OptixDenoiserLayer*)layers,
            numLayers,
            inputOffsetX,
            inputOffsetY,
            scratch,
            scratchSizeInBytes
        );
    }

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserComputeIntensity(
        OptixDenoiser handle,
        CUstream stream,
        const OptixImage2D* inputImage,
        CUdeviceptr outputIntensity,
        CUdeviceptr scratch,
        size_t scratchSizeInBytes
    ) override
    {
        return (OptixResult)::optixDenoiserComputeIntensity(
            (::OptixDenoiser)handle,
            stream,
            (const ::OptixImage2D*)inputImage,
            outputIntensity,
            scratch,
            scratchSizeInBytes
        );
    }

    virtual SLANG_NO_THROW OptixResult SLANG_MCALL optixDenoiserComputeAverageColor(
        OptixDenoiser handle,
        CUstream stream,
        const OptixImage2D* inputImage,
        CUdeviceptr outputAverageColor,
        CUdeviceptr scratch,
        size_t scratchSizeInBytes
    ) override
    {
        return (OptixResult)::optixDenoiserComputeAverageColor(
            (::OptixDenoiser)handle,
            stream,
            (const ::OptixImage2D*)inputImage,
            outputAverageColor,
            scratch,
            scratchSizeInBytes
        );
    }
};

Result createOptixDenoiserAPI(IOptixDenoiserAPI** outAPI)
{
    RefPtr<OptixDenoiserAPIImpl> api = new OptixDenoiserAPIImpl();
    SLANG_RETURN_ON_FAIL(api->init());
    returnComPtr(outAPI, api);
    return SLANG_OK;
}

} // namespace rhi::optix_denoiser::VERSION_TAG

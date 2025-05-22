#include "cuda-pipeline.h"
#include "cuda-device.h"
#include "cuda-shader-program.h"
#include "cuda-shader-object-layout.h"

namespace rhi::cuda {

ComputePipelineImpl::ComputePipelineImpl(Device* device)
    : ComputePipeline(device)
{
}

ComputePipelineImpl::~ComputePipelineImpl()
{
    if (m_module)
        SLANG_CUDA_ASSERT_ON_FAIL(cuModuleUnload(m_module));
}

Result ComputePipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::CUmodule;
    outHandle->value = (uint64_t)m_module;
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    SLANG_CUDA_CTX_SCOPE(this);

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());
    const auto& module = program->m_modules[0];

    RefPtr<ComputePipelineImpl> pipeline = new ComputePipelineImpl(this);
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;

    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuModuleLoadData(&pipeline->m_module, module.code->getBufferPointer()), this);
    pipeline->m_kernelName = module.entryPointName;
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(
        cuModuleGetFunction(&pipeline->m_function, pipeline->m_module, pipeline->m_kernelName.data()),
        this
    );
    int kernelIndex = pipeline->m_rootObjectLayout->getKernelIndex(pipeline->m_kernelName);
    SLANG_RHI_ASSERT(kernelIndex >= 0);
    pipeline->m_kernelIndex = kernelIndex;
    pipeline->m_rootObjectLayout->getKernelThreadGroupSize(kernelIndex, pipeline->m_threadGroupSize);

    // Compute the size of the parameter buffer.
    // Slang's layout computation for the CUDA parameter will align the size
    // Of the buffer to the largest parameter size.
    // cuLaunchKernel expects the parameter buffer size to be unpadded.
    size_t paramBufferSize = 0;
    for (size_t paramIndex = 0;; ++paramIndex)
    {
        size_t paramSize = 0;
        size_t paramOffset = 0;
        if (cuFuncGetParamInfo(pipeline->m_function, paramIndex, &paramOffset, &paramSize) != CUDA_SUCCESS)
        {
            break;
        }
        paramBufferSize = max(paramBufferSize, paramOffset + paramSize);
    }
    pipeline->m_paramBufferSize = paramBufferSize;

    // Query the shared memory size.
    int sharedSizeBytes = 0;
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(
        cuFuncGetAttribute(&sharedSizeBytes, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, pipeline->m_function),
        this
    );
    pipeline->m_sharedMemorySize = sharedSizeBytes;

    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

#if SLANG_RHI_ENABLE_OPTIX

RayTracingPipelineImpl::RayTracingPipelineImpl(Device* device)
    : RayTracingPipeline(device)
{
}

RayTracingPipelineImpl::~RayTracingPipelineImpl()
{
    if (m_pipeline)
        SLANG_OPTIX_ASSERT_ON_FAIL(optixPipelineDestroy(m_pipeline));
    for (OptixProgramGroup programGroup : m_programGroups)
        SLANG_OPTIX_ASSERT_ON_FAIL(optixProgramGroupDestroy(programGroup));
    for (OptixModule module : m_modules)
        SLANG_OPTIX_ASSERT_ON_FAIL(optixModuleDestroy(module));
}

Result RayTracingPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::OptixPipeline;
    outHandle->value = (uint64_t)m_pipeline;
    return SLANG_OK;
}

Result DeviceImpl::createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    SLANG_CUDA_CTX_SCOPE(this);

    if (!m_ctx.optixContext)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());

    OptixPipelineCompileOptions optixPipelineCompileOptions = {};
    optixPipelineCompileOptions.usesMotionBlur = 0;
    optixPipelineCompileOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
    optixPipelineCompileOptions.numPayloadValues = (desc.maxRayPayloadSize + sizeof(uint32_t) - 1) / sizeof(uint32_t);
    optixPipelineCompileOptions.numAttributeValues =
        (desc.maxAttributeSizeInBytes + sizeof(uint32_t) - 1) / sizeof(uint32_t);
    optixPipelineCompileOptions.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    optixPipelineCompileOptions.pipelineLaunchParamsVariableName = "SLANG_globalParams";
    // TODO not sure if removing support for certain types is the same as "skipping" in DXR/Vulkan
    optixPipelineCompileOptions.usesPrimitiveTypeFlags = 0;
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
                m_ctx.optixContext,
                &optixModuleCompileOptions,
                &optixPipelineCompileOptions,
                static_cast<const char*>(module.code->getBufferPointer()),
                module.code->getBufferSize(),
                nullptr,
                0,
                &optixModules.emplace_back()
            ),
            this
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
                m_ctx.optixContext,
                &optixProgramGroupDesc,
                1,
                &optixProgramGroupOptions,
                nullptr,
                0,
                &optixProgramGroups.emplace_back()
            ),
            this
        );
        shaderGroupNameToIndex[module.entryPointName] = optixProgramGroups.size() - 1;
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
            optixProgramGroupDesc.hitgroup.moduleIS =
                optixModules[entryPointNameToModuleIndex[hitGroupDesc.intersectionEntryPoint]];
            entryFunctionNameIS = std::string("__intersection__") + hitGroupDesc.intersectionEntryPoint;
            optixProgramGroupDesc.hitgroup.entryFunctionNameIS = entryFunctionNameIS.data();
        }
        SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
            optixProgramGroupCreate(
                m_ctx.optixContext,
                &optixProgramGroupDesc,
                1,
                &optixProgramGroupOptions,
                nullptr,
                0,
                &optixProgramGroups.emplace_back()
            ),
            this
        );
        shaderGroupNameToIndex[hitGroupDesc.hitGroupName] = optixProgramGroups.size() - 1;
    }

    OptixPipeline optixPipeline = nullptr;
    OptixPipelineLinkOptions optixPipelineLinkOptions = {};
    optixPipelineLinkOptions.maxTraceDepth = desc.maxRecursion;

    SLANG_OPTIX_RETURN_ON_FAIL_REPORT(
        optixPipelineCreate(
            m_ctx.optixContext,
            &optixPipelineCompileOptions,
            &optixPipelineLinkOptions,
            optixProgramGroups.data(),
            optixProgramGroups.size(),
            nullptr,
            0,
            &optixPipeline
        ),
        this
    );

    RefPtr<RayTracingPipelineImpl> pipeline = new RayTracingPipelineImpl(this);
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_modules = std::move(optixModules);
    pipeline->m_programGroups = std::move(optixProgramGroups);
    pipeline->m_shaderGroupNameToIndex = std::move(shaderGroupNameToIndex);
    pipeline->m_pipeline = optixPipeline;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

#else // SLANG_RHI_ENABLE_OPTIX

Result DeviceImpl::createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outPipeline);
    return SLANG_E_NOT_AVAILABLE;
}

#endif // SLANG_RHI_ENABLE_OPTIX

} // namespace rhi::cuda

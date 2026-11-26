#include "cuda-pipeline.h"
#include "cuda-device.h"
#include "cuda-shader-program.h"
#include "cuda-shader-object-layout.h"
#include "cuda-utils.h"

// Enable using cuModuleLoadDataEx for loading CUDA modules.
// This allows us to get logs for module loading.
#define SLANG_RHI_CUDA_DEBUG_MODULE_LOAD 0

namespace rhi::cuda {

ComputePipelineImpl::ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc)
    : ComputePipeline(device, desc)
{
}

ComputePipelineImpl::~ComputePipelineImpl()
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    if (m_module)
        SLANG_CUDA_ASSERT_ON_FAIL(cuModuleUnload(m_module));
}

Result ComputePipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::CUmodule;
    outHandle->value = (uint64_t)m_module;
    return SLANG_OK;
}

Result ComputePipelineImpl::getMaxCooperativeThreadGroupCount(IShaderObject* rootObject, uint32_t* outCount)
{
    SLANG_UNUSED(rootObject);
    *outCount = m_maxCooperativeThreadGroupCount;
    return m_cooperative ? SLANG_OK : SLANG_E_NOT_AVAILABLE;
}

Result DeviceImpl::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    TimePoint startTime = Timer::now();

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    SLANG_RHI_ASSERT(!program->m_modules.empty());
    const auto& module = program->m_modules[0];

    RefPtr<ComputePipelineImpl> pipeline = new ComputePipelineImpl(this, desc);
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;

#if SLANG_RHI_CUDA_DEBUG_MODULE_LOAD
    // Setup buffers for info and error logs.
    size_t infoLogSize = 16 * 1024;
    size_t errorLogSize = 16 * 1024;
    int logVerbose = 1;
    auto infoLog = std::make_unique<uint8_t[]>(infoLogSize);
    auto errorLog = std::make_unique<uint8_t[]>(errorLogSize);

    CUjit_option options[] = {
        CU_JIT_INFO_LOG_BUFFER,
        CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES,
        CU_JIT_ERROR_LOG_BUFFER,
        CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES,
        CU_JIT_LOG_VERBOSE,
    };
    void* optionValues[] = {
        infoLog.get(),
        (void*)(uintptr_t)infoLogSize,
        errorLog.get(),
        (void*)(uintptr_t)errorLogSize,
        (void*)(uintptr_t)logVerbose,
    };
    CUresult result =
        cuModuleLoadDataEx(&pipeline->m_module, module.code.c_str(), SLANG_COUNT_OF(options), options, optionValues);
    infoLogSize = *(unsigned int*)(&optionValues[1]);
    errorLogSize = *(unsigned int*)(&optionValues[3]);
    if (infoLogSize > 0)
    {
        printInfo("Info log from cuModuleLoadDataEx:\n%s", infoLog.get());
    }
    if (errorLogSize > 0)
    {
        printError("Error log from cuModuleLoadDataEx:\n%s", errorLog.get());
    }
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(result, this);
#else  // SLANG_RHI_CUDA_DEBUG_MODULE_LOAD
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuModuleLoadData(&pipeline->m_module, module.code.c_str()), this);
#endif // SLANG_RHI_CUDA_DEBUG_MODULE_LOAD
    pipeline->m_entryPointName = module.entryPointName;
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(
        cuModuleGetFunction(&pipeline->m_function, pipeline->m_module, pipeline->m_entryPointName.data()),
        this
    );
    int entryPointIndex = pipeline->m_rootObjectLayout->getEntryPointIndex(pipeline->m_entryPointName);
    SLANG_RHI_ASSERT(entryPointIndex >= 0);
    pipeline->m_entryPointIndex = entryPointIndex;
    pipeline->m_rootObjectLayout->getEntryPointThreadGroupSize(entryPointIndex, pipeline->m_threadGroupSize);

    // Get the global `SLANG_globalParams` address and size.
    if (cuModuleGetGlobal(
            &pipeline->m_globalParams,
            &pipeline->m_globalParamsSize,
            pipeline->m_module,
            "SLANG_globalParams"
        ) != CUDA_SUCCESS)
    {
        pipeline->m_globalParams = 0;
        pipeline->m_globalParamsSize = 0;
    }

    // Query the shared memory size.
    int sharedSizeBytes = 0;
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(
        cuFuncGetAttribute(&sharedSizeBytes, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, pipeline->m_function),
        this
    );
    pipeline->m_sharedMemorySize = sharedSizeBytes;

    // Cooperative launch: the co-resident group limit is the occupancy per SM times the SM count.
    if (is_set(desc.flags, ComputePipelineFlags::CooperativeLaunch))
    {
        int blocksPerSM = 0;
        int smCount = 0;
        uint32_t threads =
            pipeline->m_threadGroupSize[0] * pipeline->m_threadGroupSize[1] * pipeline->m_threadGroupSize[2];
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(
            cuOccupancyMaxActiveBlocksPerMultiprocessor(
                &blocksPerSM,
                pipeline->m_function,
                int(threads),
                pipeline->m_dynamicSharedMemorySize
            ),
            this
        );
        if (blocksPerSM == 0)
        {
            printError(
                "Cooperative launch: no thread group of pipeline %s fits on a multiprocessor (%u threads, %d bytes "
                "of shared memory), so every dispatch would fail. Reduce the thread group size, or the registers "
                "or shared memory the kernel uses.",
                pipeline->m_entryPointName.c_str(),
                threads,
                sharedSizeBytes + int(pipeline->m_dynamicSharedMemorySize)
            );
            return SLANG_FAIL;
        }
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(
            cuDeviceGetAttribute(&smCount, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, m_ctx.device),
            this
        );
        // Kernel parameter offsets, for passing the entry point data as kernelParams.
        for (size_t paramIndex = 0;; ++paramIndex)
        {
            size_t paramOffset = 0;
            size_t paramSize = 0;
            if (cuFuncGetParamInfo(pipeline->m_function, paramIndex, &paramOffset, &paramSize) != CUDA_SUCCESS)
                break;
            pipeline->m_kernelParamOffsets.push_back(paramOffset);
        }
        pipeline->m_cooperative = true;
        pipeline->m_maxCooperativeThreadGroupCount = uint32_t(blocksPerSM) * uint32_t(smCount);
    }

    // Report the pipeline creation time.
    if (m_shaderCompilationReporter)
    {
        m_shaderCompilationReporter->reportCreatePipeline(
            program,
            ShaderCompilationReporter::PipelineType::Compute,
            startTime,
            Timer::now(),
            false,
            0,
            nullptr
        );
    }

    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

RayTracingPipelineImpl::RayTracingPipelineImpl(Device* device, const RayTracingPipelineDesc& desc)
    : RayTracingPipeline(device, desc)
{
}

RayTracingPipelineImpl::~RayTracingPipelineImpl()
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    m_optixPipeline.setNull();
}

Result RayTracingPipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::OptixPipeline;
    outHandle->value = m_optixPipeline->getNativeHandle();
    return SLANG_OK;
}

Result DeviceImpl::createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    if (!m_ctx.optixContext)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    RefPtr<optix::Pipeline> optixPipeline;
    SLANG_RETURN_ON_FAIL(
        m_ctx.optixContext->createPipeline(desc, m_shaderCompilationReporter, optixPipeline.writeRef())
    );

    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);

    RefPtr<RayTracingPipelineImpl> pipeline = new RayTracingPipelineImpl(this, desc);
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;
    pipeline->m_optixPipeline = optixPipeline;
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

} // namespace rhi::cuda

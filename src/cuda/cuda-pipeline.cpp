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

Result DeviceImpl::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    SLANG_CUDA_CTX_SCOPE(this);

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
    CUresult result = cuModuleLoadDataEx(
        &pipeline->m_module,
        module.code->getBufferPointer(),
        SLANG_COUNT_OF(options),
        options,
        optionValues
    );
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
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuModuleLoadData(&pipeline->m_module, module.code->getBufferPointer()), this);
#endif // SLANG_RHI_CUDA_DEBUG_MODULE_LOAD
    pipeline->m_kernelName = module.entryPointName;
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(
        cuModuleGetFunction(&pipeline->m_function, pipeline->m_module, pipeline->m_kernelName.data()),
        this
    );
    int kernelIndex = pipeline->m_rootObjectLayout->getKernelIndex(pipeline->m_kernelName);
    SLANG_RHI_ASSERT(kernelIndex >= 0);
    pipeline->m_kernelIndex = kernelIndex;
    pipeline->m_rootObjectLayout->getKernelThreadGroupSize(kernelIndex, pipeline->m_threadGroupSize);

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

    // Report the pipeline creation time.
    if (m_shaderCompilationReporter)
    {
        m_shaderCompilationReporter->reportCreatePipeline(
            program,
            ShaderCompilationReporter::PipelineType::Compute,
            startTime,
            Timer::now(),
            false,
            0
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
    SLANG_CUDA_CTX_SCOPE(this);

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

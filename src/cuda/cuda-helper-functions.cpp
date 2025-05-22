#include "cuda-helper-functions.h"
#include "cuda-device.h"

namespace rhi::cuda {

#if SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK
static thread_local CUcontext g_currentContext = nullptr;
static thread_local std::atomic<uint32_t> g_contextStackDepth = 0;
#endif

ContextScope::ContextScope(const DeviceImpl* device)
{
    SLANG_CUDA_ASSERT_ON_FAIL(cuCtxPushCurrent(device->m_ctx.context));
#if SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK
    g_currentContext = device->m_ctx.context;
    g_contextStackDepth++;
#endif
}

ContextScope::~ContextScope()
{
    CUcontext ctx;
    SLANG_CUDA_ASSERT_ON_FAIL(cuCtxPopCurrent(&ctx));
#if SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK
    g_currentContext = ctx;
    g_contextStackDepth--;
#endif
}

#if SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK
CUcontext getCurrentContext()
{
    return g_contextStackDepth.load() > 0 ? g_currentContext : nullptr;
}
#endif

void _reportCUDAError(
    CUresult result,
    const char* call,
    const char* file,
    int line,
    DebugCallbackAdapter debug_callback
)
{
    if (!debug_callback)
        return;

    const char* errorString = nullptr;
    const char* errorName = nullptr;
    cuGetErrorString(result, &errorString);
    cuGetErrorName(result, &errorName);
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s failed: %s (%s)\nAt %s:%d\n", call, errorString, errorName, file, line);
    debug_callback->handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, buf);
}

void _reportCUDAAssert(CUresult result, const char* call, const char* file, int line)
{
    const char* errorString = nullptr;
    const char* errorName = nullptr;
    cuGetErrorString(result, &errorString);
    cuGetErrorName(result, &errorName);
    std::fprintf(stderr, "%s failed: %s (%s)\n", call, errorString, errorName);
}

#if SLANG_RHI_ENABLE_OPTIX

void _reportOptixError(
    OptixResult result,
    const char* call,
    const char* file,
    int line,
    DebugCallbackAdapter debug_callback
)
{
    if (!debug_callback)
        return;

    char buf[4096];
    snprintf(
        buf,
        sizeof(buf),
        "%s failed: %s (%s)\nAt %s:%d\n",
        call,
        optixGetErrorString(result),
        optixGetErrorName(result),
        file,
        line
    );
    debug_callback->handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, buf);
}

void _reportOptixAssert(OptixResult result, const char* call, const char* file, int line)
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

#endif // SLANG_RHI_ENABLE_OPTIX

AdapterLUID getAdapterLUID(int deviceIndex)
{
    CUdevice device;
    SLANG_CUDA_ASSERT_ON_FAIL(cuDeviceGet(&device, deviceIndex));
    AdapterLUID luid = {};
#if SLANG_WINDOWS_FAMILY
    unsigned int deviceNodeMask;
    SLANG_CUDA_ASSERT_ON_FAIL(cuDeviceGetLuid((char*)&luid, &deviceNodeMask, device));
#elif SLANG_LINUX_FAMILY
    SLANG_CUDA_ASSERT_ON_FAIL(cuDeviceGetUuid((CUuuid*)&luid, device));
#else
#error "Unsupported platform"
#endif
    return luid;
}

Result SLANG_MCALL getAdapters(std::vector<AdapterInfo>& outAdapters)
{
    if (!rhiCudaDriverApiInit())
    {
        return SLANG_FAIL;
    }
    SLANG_CUDA_RETURN_ON_FAIL(cuInit(0));
    int deviceCount;
    SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetCount(&deviceCount));
    for (int deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++)
    {
        CUdevice device;
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGet(&device, deviceIndex));

        AdapterInfo info = {};
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetName(info.name, sizeof(info.name), device));
        info.luid = getAdapterLUID(deviceIndex);
        outAdapters.push_back(info);
    }

    return SLANG_OK;
}

} // namespace rhi::cuda

namespace rhi {

Result SLANG_MCALL getCUDAAdapters(std::vector<AdapterInfo>& outAdapters)
{
    return cuda::getAdapters(outAdapters);
}

Result SLANG_MCALL createCUDADevice(const DeviceDesc* desc, IDevice** outDevice)
{
    RefPtr<cuda::DeviceImpl> result = new cuda::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi

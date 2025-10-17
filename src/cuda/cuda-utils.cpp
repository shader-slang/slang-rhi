#include "cuda-utils.h"
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

void checkCurrentContext()
{
    CUcontext currentContext = nullptr;
    cuCtxGetCurrent(&currentContext);
    CUcontext expectedContext = getCurrentContext();
    if (expectedContext && expectedContext != currentContext)
    {
        __debugbreak();
    }
}
#endif

#if SLANG_RHI_ENABLE_CUDA_SYNC_ERROR_CHECK

// Helper to check if a result is an error, filtering out ones that occur when cuCtxSynchronize is called
// outside of a valid context.
bool isCUDASyncError(CUresult result)
{
    return isCUDAError(result) && result != CUDA_ERROR_NOT_INITIALIZED && result != CUDA_ERROR_INVALID_CONTEXT;
}

// Sync full CUDA and check for errors, asserting if any are found.
void checkCudaSyncError(bool pre, const char* call, const char* file, int line)
{
    CUresult result = cuCtxSynchronize();
    if (isCUDASyncError(result))
    {
        reportCUDAAssert(result, call, file, line);
        if (pre)
        {
            std::fprintf(
                stderr,
                "Error detected BEFORE the call, suggesting a prior, uncaptured CUDA call is responsible\n"
            );
        }
        else
        {
            std::fprintf(stderr, "Error detected AFTER the call, suggesting it is responsible\n");
        }
        ::rhi::handleAssert("CUDA error detected", file, line);
    }
}

// Sync full CUDA and check for errors, reporting to device if any are found.
void checkCudaSyncErrorReport(bool pre, const char* call, const char* file, int line, DeviceAdapter device)
{
    CUresult result = cuCtxSynchronize();
    if (isCUDASyncError(result))
    {
        reportCUDAError(result, call, file, line, device);
        if (pre)
        {
            device->handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Driver,
                "Error detected BEFORE the call, suggesting a prior, uncaptured CUDA call is responsible\n"
            );
        }
        else
        {
            device->handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Driver,
                "Error detected AFTER the call, suggesting it is responsible\n"
            );
        }
    }
}
#endif

void reportCUDAError(CUresult result, const char* call, const char* file, int line, DeviceAdapter device)
{
    if (!device)
        return;

    const char* errorString = nullptr;
    const char* errorName = nullptr;
    cuGetErrorString(result, &errorString);
    cuGetErrorName(result, &errorName);
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s failed: %s (%s)\nAt %s:%d\n", call, errorString, errorName, file, line);
    buf[sizeof(buf) - 1] = 0; // Ensure null termination
    device->handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, buf);
}

void reportCUDAAssert(CUresult result, const char* call, const char* file, int line)
{
    const char* errorString = nullptr;
    const char* errorName = nullptr;
    cuGetErrorString(result, &errorString);
    cuGetErrorName(result, &errorName);
    std::fprintf(stderr, "%s failed: %s (%s)\n", call, errorString, errorName);
}

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

} // namespace rhi::cuda

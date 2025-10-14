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

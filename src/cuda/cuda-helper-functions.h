#pragma once

#include "cuda-base.h"

/// Enable CUDA context check.
/// This is useful for debugging to ensure that the CUDA context is set correctly when calling CUDA APIs.
#define SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK 0

namespace rhi::cuda {

/// Helper class to push/pop CUDA context on the stack.
class ContextScope
{
public:
    explicit ContextScope(const DeviceImpl* device);
    ~ContextScope();
};

#define SLANG_CUDA_CTX_SCOPE(device) rhi::cuda::ContextScope _context_scope(device)

#if SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK
CUcontext getCurrentContext();
#endif

#if SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK
inline void SLANG_RHI_CHECK_CUDA_CTX()
{
    CUcontext currentContext = nullptr;
    cuCtxGetCurrent(&currentContext);
    CUcontext expectedContext = getCurrentContext();
    if (expectedContext && expectedContext != currentContext)
    {
        __debugbreak();
    }
}
#else
#define SLANG_RHI_CHECK_CUDA_CTX()
#endif

inline bool _isError(CUresult result)
{
    return result != 0;
}

void _reportCUDAError(
    CUresult result,
    const char* call,
    const char* file,
    int line,
    DebugCallbackAdapter debug_callback
);

void _reportCUDAAssert(CUresult result, const char* call, const char* file, int line);

#define SLANG_CUDA_RETURN_ON_FAIL(x)                                                                                   \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        auto _res = x;                                                                                                 \
        if (_isError(_res))                                                                                            \
        {                                                                                                              \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

#define SLANG_CUDA_RETURN_ON_FAIL_REPORT(x, debug_callback)                                                            \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        auto _res = x;                                                                                                 \
        if (_isError(_res))                                                                                            \
        {                                                                                                              \
            _reportCUDAError(_res, #x, __FILE__, __LINE__, debug_callback);                                            \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

#define SLANG_CUDA_ASSERT_ON_FAIL(x)                                                                                   \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        auto _res = x;                                                                                                 \
        if (_isError(_res))                                                                                            \
        {                                                                                                              \
            _reportCUDAAssert(_res, #x, __FILE__, __LINE__);                                                           \
            SLANG_RHI_ASSERT_FAILURE("CUDA call failed");                                                              \
        }                                                                                                              \
    }

#if SLANG_RHI_ENABLE_OPTIX

inline bool _isError(OptixResult result)
{
    return result != OPTIX_SUCCESS;
}

void _reportOptixError(
    OptixResult result,
    const char* call,
    const char* file,
    int line,
    DebugCallbackAdapter debug_callback
);

void _reportOptixAssert(OptixResult result, const char* call, const char* file, int line);

#define SLANG_OPTIX_RETURN_ON_FAIL(x)                                                                                  \
    {                                                                                                                  \
        auto _res = x;                                                                                                 \
        if (_isError(_res))                                                                                            \
        {                                                                                                              \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

#define SLANG_OPTIX_RETURN_ON_FAIL_REPORT(x, debug_callback)                                                           \
    {                                                                                                                  \
        auto _res = x;                                                                                                 \
        if (_isError(_res))                                                                                            \
        {                                                                                                              \
            _reportOptixError(_res, #x, __FILE__, __LINE__, debug_callback);                                           \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

#define SLANG_OPTIX_ASSERT_ON_FAIL(x)                                                                                  \
    {                                                                                                                  \
        auto _res = x;                                                                                                 \
        if (_isError(_res))                                                                                            \
        {                                                                                                              \
            _reportOptixAssert(_res, #x, __FILE__, __LINE__);                                                          \
            SLANG_RHI_ASSERT_FAILURE("OptiX call failed");                                                             \
        }                                                                                                              \
    }

void _optixLogCallback(unsigned int level, const char* tag, const char* message, void* userData);

#endif // SLANG_RHI_ENABLE_OPTIX

AdapterLUID getAdapterLUID(int deviceIndex);

} // namespace rhi::cuda

namespace rhi {

Result SLANG_MCALL getCUDAAdapters(std::vector<AdapterInfo>& outAdapters);

Result SLANG_MCALL createCUDADevice(const DeviceDesc* desc, IDevice** outDevice);

} // namespace rhi

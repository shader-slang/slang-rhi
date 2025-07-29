#pragma once

#include "../rhi-shared.h"

#include "cuda-api.h"

/// Enable CUDA context check.
/// This is useful for debugging to ensure that the CUDA context is set correctly when calling CUDA APIs.
#define SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK 0

namespace rhi::cuda {

class DeviceImpl;

/// Helper class to push/pop CUDA context on the stack.
class ContextScope
{
public:
    explicit ContextScope(const DeviceImpl* device);
    ~ContextScope();
};

#define SLANG_CUDA_CTX_SCOPE(device) ::rhi::cuda::ContextScope _context_scope(device)

#if SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK
CUcontext getCurrentContext();
void checkCurrentContext();
#endif

#if SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK
#define SLANG_RHI_CHECK_CUDA_CTX() ::rhi::cuda::checkCurrentContext()
#else
#define SLANG_RHI_CHECK_CUDA_CTX()
#endif

inline bool isCUDAError(CUresult result)
{
    return result != 0;
}

void reportCUDAError(
    CUresult result,
    const char* call,
    const char* file,
    int line,
    DebugCallbackAdapter debug_callback
);

void reportCUDAAssert(CUresult result, const char* call, const char* file, int line);

#define SLANG_CUDA_RETURN_ON_FAIL(x)                                                                                   \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::isCUDAError(_res))                                                                            \
        {                                                                                                              \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

#define SLANG_CUDA_RETURN_ON_FAIL_REPORT(x, debug_callback)                                                            \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::isCUDAError(_res))                                                                            \
        {                                                                                                              \
            ::rhi::cuda::reportCUDAError(_res, #x, __FILE__, __LINE__, debug_callback);                                \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

#define SLANG_CUDA_ASSERT_ON_FAIL(x)                                                                                   \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::isCUDAError(_res))                                                                            \
        {                                                                                                              \
            ::rhi::cuda::reportCUDAAssert(_res, #x, __FILE__, __LINE__);                                               \
            SLANG_RHI_ASSERT_FAILURE("CUDA call failed");                                                              \
        }                                                                                                              \
    }

#if SLANG_RHI_ENABLE_OPTIX

inline bool isOptixError(OptixResult result)
{
    return result != OPTIX_SUCCESS;
}

void reportOptixError(
    OptixResult result,
    const char* call,
    const char* file,
    int line,
    DebugCallbackAdapter debug_callback
);

void reportOptixAssert(OptixResult result, const char* call, const char* file, int line);

#define SLANG_OPTIX_RETURN_ON_FAIL(x)                                                                                  \
    {                                                                                                                  \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::isOptixError(_res))                                                                           \
        {                                                                                                              \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

#define SLANG_OPTIX_RETURN_ON_FAIL_REPORT(x, debug_callback)                                                           \
    {                                                                                                                  \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::isOptixError(_res))                                                                           \
        {                                                                                                              \
            ::rhi::cuda::reportOptixError(_res, #x, __FILE__, __LINE__, debug_callback);                               \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

#define SLANG_OPTIX_ASSERT_ON_FAIL(x)                                                                                  \
    {                                                                                                                  \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::isOptixError(_res))                                                                           \
        {                                                                                                              \
            ::rhi::cuda::reportOptixAssert(_res, #x, __FILE__, __LINE__);                                              \
            SLANG_RHI_ASSERT_FAILURE("OptiX call failed");                                                             \
        }                                                                                                              \
    }

#endif // SLANG_RHI_ENABLE_OPTIX

AdapterLUID getAdapterLUID(int deviceIndex);

} // namespace rhi::cuda

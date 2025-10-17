#pragma once

#include "../rhi-shared.h"

#include "cuda-api.h"

/// Enable CUDA context check.
/// This is useful for debugging to ensure that the CUDA context is set correctly when calling CUDA APIs.
#define SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK 0

/// Enable synchronous CUDA error checking by calling cuCtxSynchronize after each CUDA call and checking
/// for errors. This is very slow, but useful for tracking down CUDA errors that are triggered by
/// asynchronous operations.
#define SLANG_RHI_ENABLE_CUDA_SYNC_ERROR_CHECK 0

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

#if SLANG_RHI_ENABLE_CUDA_SYNC_ERROR_CHECK
void checkCudaSyncError(bool pre, const char* call, const char* file, int line);
void checkCudaSyncErrorReport(bool pre, const char* call, const char* file, int line, DeviceAdapter device);
#define SLANG_CUDA_CHECK_SYNC_ERROR(pre, call) ::rhi::cuda::checkCudaSyncError(pre, call, __FILE__, __LINE__)
#define SLANG_CUDA_CHECK_SYNC_ERROR_REPORT(pre, call, device)                                                          \
    ::rhi::cuda::checkCudaSyncErrorReport(pre, call, __FILE__, __LINE__, device)
#else
#define SLANG_CUDA_CHECK_SYNC_ERROR(pre, call)
#define SLANG_CUDA_CHECK_SYNC_ERROR_REPORT(pre, call, device)
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

void reportCUDAError(CUresult result, const char* call, const char* file, int line, DeviceAdapter device);

void reportCUDAAssert(CUresult result, const char* call, const char* file, int line);

#define SLANG_CUDA_RETURN_ON_FAIL(x)                                                                                   \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        SLANG_CUDA_CHECK_SYNC_ERROR(true, #x);                                                                         \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::isCUDAError(_res))                                                                            \
        {                                                                                                              \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
        SLANG_CUDA_CHECK_SYNC_ERROR(false, #x);                                                                        \
    }

#define SLANG_CUDA_RETURN_ON_FAIL_REPORT(x, device)                                                                    \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        SLANG_CUDA_CHECK_SYNC_ERROR_REPORT(true, #x, device);                                                          \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::isCUDAError(_res))                                                                            \
        {                                                                                                              \
            ::rhi::cuda::reportCUDAError(_res, #x, __FILE__, __LINE__, device);                                        \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
        SLANG_CUDA_CHECK_SYNC_ERROR_REPORT(false, #x, device);                                                         \
    }

#define SLANG_CUDA_ASSERT_ON_FAIL(x)                                                                                   \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        SLANG_CUDA_CHECK_SYNC_ERROR(true, #x);                                                                         \
        auto _res = x;                                                                                                 \
        if (::rhi::cuda::isCUDAError(_res))                                                                            \
        {                                                                                                              \
            ::rhi::cuda::reportCUDAAssert(_res, #x, __FILE__, __LINE__);                                               \
            SLANG_RHI_ASSERT_FAILURE("CUDA call failed");                                                              \
        }                                                                                                              \
        SLANG_CUDA_CHECK_SYNC_ERROR(false, #x);                                                                        \
    }

AdapterLUID getAdapterLUID(int deviceIndex);

} // namespace rhi::cuda

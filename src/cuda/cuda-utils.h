#pragma once

#include "../rhi-shared.h"

#include "cuda-api.h"

#include "core/diagnostics.h"

#include <cmath>

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
/// Checks if the correct context is already current before pushing (optimization).
class ContextScope
{
public:
    explicit ContextScope(const DeviceImpl* device);
    ~ContextScope();

    bool m_didPush = false;
};

#define SLANG_CUDA_CTX_SCOPE(device) ::rhi::cuda::ContextScope _context_scope(device)

#if SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK
CUcontext getCurrentContext();
void checkCurrentContext();
#endif

#if SLANG_RHI_ENABLE_CUDA_SYNC_ERROR_CHECK
void checkCudaSyncError(bool pre, const char* call, const SourceLocation location);
void checkCudaSyncErrorReport(bool pre, const char* call, const SourceLocation location, Device* device);
#define SLANG_CUDA_CHECK_SYNC_ERROR(pre, call) ::rhi::cuda::checkCudaSyncError(pre, call, SLANG_RHI_SOURCE_LOCATION())
/// Pass nullptr for device to write the diagnostic to stderr.
#define SLANG_CUDA_CHECK_SYNC_ERROR_REPORT(pre, call, device)                                                          \
    ::rhi::cuda::checkCudaSyncErrorReport(pre, call, SLANG_RHI_SOURCE_LOCATION(), getDiagnosticDevice(device))
#else
#define SLANG_CUDA_CHECK_SYNC_ERROR(pre, call)
#define SLANG_CUDA_CHECK_SYNC_ERROR_REPORT(pre, call, device)
#endif

#if SLANG_RHI_ENABLE_CUDA_CONTEXT_CHECK
#define SLANG_RHI_CHECK_CUDA_CTX() ::rhi::cuda::checkCurrentContext()
#else
#define SLANG_RHI_CHECK_CUDA_CTX()
#endif

void reportCUDAError(CUresult result, const char* call, const SourceLocation location, Device* device = nullptr);

#define SLANG_CUDA_RETURN_ON_FAIL(x)                                                                                   \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        SLANG_CUDA_CHECK_SYNC_ERROR(true, #x);                                                                         \
        CUresult _res = x;                                                                                             \
        if (_res != CUDA_SUCCESS)                                                                                      \
        {                                                                                                              \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
        SLANG_CUDA_CHECK_SYNC_ERROR(false, #x);                                                                        \
    }

/// Pass nullptr for device to write the diagnostic to stderr.
#define SLANG_CUDA_RETURN_ON_FAIL_REPORT(x, device)                                                                    \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        SLANG_CUDA_CHECK_SYNC_ERROR_REPORT(true, #x, device);                                                          \
        CUresult _res = x;                                                                                             \
        if (_res != CUDA_SUCCESS)                                                                                      \
        {                                                                                                              \
            ::rhi::cuda::reportCUDAError(_res, #x, SLANG_RHI_SOURCE_LOCATION(), getDiagnosticDevice(device));          \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
        SLANG_CUDA_CHECK_SYNC_ERROR_REPORT(false, #x, device);                                                         \
    }

#define SLANG_CUDA_ASSERT_ON_FAIL(x)                                                                                   \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        SLANG_CUDA_CHECK_SYNC_ERROR(true, #x);                                                                         \
        CUresult _res = x;                                                                                             \
        if (_res != CUDA_SUCCESS)                                                                                      \
        {                                                                                                              \
            ::rhi::cuda::reportCUDAError(_res, #x, SLANG_RHI_SOURCE_LOCATION());                                       \
            SLANG_RHI_ASSERT_FAILURE("CUDA call failed");                                                              \
        }                                                                                                              \
        SLANG_CUDA_CHECK_SYNC_ERROR(false, #x);                                                                        \
    }

AdapterLUID getAdapterLUID(int deviceIndex);

inline uint64_t cudaEventMillisecondsToMicroseconds(float elapsedMs)
{
    return uint64_t(std::llround(double(elapsedMs) * 1000.0));
}

} // namespace rhi::cuda

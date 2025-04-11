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

inline bool _isError(CUresult result)
{
    return result != 0;
}

// A enum used to control if errors are reported on failure of CUDA call.
enum class CUDAReportStyle
{
    Normal,
    Silent,
};

struct CUDAErrorInfo
{
    CUDAErrorInfo(const char* filePath, int lineNo, const char* errorName = nullptr, const char* errorString = nullptr)
        : m_filePath(filePath)
        , m_lineNo(lineNo)
        , m_errorName(errorName)
        , m_errorString(errorString)
    {
    }
    Result handle() const;

    const char* m_filePath;
    int m_lineNo;
    const char* m_errorName;
    const char* m_errorString;
};

// If this code path is enabled, CUDA errors will be reported directly to StdWriter::out stream.

Result _handleCUDAError(CUresult cuResult, const char* file, int line);

#define SLANG_CUDA_HANDLE_ERROR(x) _handleCUDAError(x, __FILE__, __LINE__)

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


#define SLANG_CUDA_RETURN_ON_FAIL(x)                                                                                   \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        auto _res = x;                                                                                                 \
        if (_isError(_res))                                                                                            \
            return SLANG_CUDA_HANDLE_ERROR(_res);                                                                      \
    }

#define SLANG_CUDA_RETURN_WITH_REPORT_ON_FAIL(x, r)                                                                    \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        auto _res = x;                                                                                                 \
        if (_isError(_res))                                                                                            \
        {                                                                                                              \
            return (r == CUDAReportStyle::Normal) ? SLANG_CUDA_HANDLE_ERROR(_res) : SLANG_FAIL;                        \
        }                                                                                                              \
    }

#define SLANG_CUDA_ASSERT_ON_FAIL(x)                                                                                   \
    {                                                                                                                  \
        SLANG_RHI_CHECK_CUDA_CTX();                                                                                    \
        auto _res = x;                                                                                                 \
        if (_isError(_res))                                                                                            \
        {                                                                                                              \
            SLANG_RHI_ASSERT_FAILURE("Failed CUDA call");                                                              \
        };                                                                                                             \
    }

#if SLANG_RHI_ENABLE_OPTIX

inline bool _isError(OptixResult result)
{
    return result != OPTIX_SUCCESS;
}

#if 1
Result _handleOptixError(OptixResult result, const char* file, int line);

#define SLANG_OPTIX_HANDLE_ERROR(RESULT) _handleOptixError(RESULT, __FILE__, __LINE__)
#else
#define SLANG_OPTIX_HANDLE_ERROR(RESULT) SLANG_FAIL
#endif

#define SLANG_OPTIX_RETURN_ON_FAIL(x)                                                                                  \
    {                                                                                                                  \
        auto _res = x;                                                                                                 \
        if (_isError(_res))                                                                                            \
            return SLANG_OPTIX_HANDLE_ERROR(_res);                                                                     \
    }

#define SLANG_OPTIX_ASSERT_ON_FAIL(x)                                                                                  \
    {                                                                                                                  \
        auto _res = x;                                                                                                 \
        if (_isError(_res))                                                                                            \
        {                                                                                                              \
            SLANG_RHI_ASSERT_FAILURE("Failed OptiX call");                                                             \
        };                                                                                                             \
    }

void _optixLogCallback(unsigned int level, const char* tag, const char* message, void* userData);

#endif // SLANG_RHI_ENABLE_OPTIX

AdapterLUID getAdapterLUID(int deviceIndex);

} // namespace rhi::cuda

namespace rhi {

Result SLANG_MCALL getCUDAAdapters(std::vector<AdapterInfo>& outAdapters);

Result SLANG_MCALL createCUDADevice(const DeviceDesc* desc, IDevice** outDevice);

} // namespace rhi

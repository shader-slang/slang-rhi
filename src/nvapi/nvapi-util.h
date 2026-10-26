#pragma once

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include "nvapi-include.h"
#include "core/assert.h"

namespace rhi {

class Device;

struct NVAPIShaderExtension
{
    uint32_t uavSlot = uint32_t(-1);
    uint32_t registerSpace = 0;
    explicit operator bool() const { return uavSlot != uint32_t(-1); }
};

struct NVAPIUtil
{
    /// Set up NVAPI for use. Must be called before any other function is used.
    static Result initialize();

    /// True if the NVAPI is available, can be called even if initialize fails.
    /// If initialize has not been called will return false
    static bool isAvailable();

    static NVAPIShaderExtension findShaderExtension(slang::ProgramLayout* layout);

    static Result handleFail(int res, const char* file, int line, const char* call, Device* device = nullptr);
};

#define SLANG_RHI_NVAPI_RETURN_ON_FAIL(x) SLANG_RHI_NVAPI_RETURN_ON_FAIL_REPORT(x, nullptr)

#define SLANG_RHI_NVAPI_RETURN_ON_FAIL_REPORT(x, device)                                                               \
    {                                                                                                                  \
        NvAPI_Status _res = x;                                                                                         \
        if (_res != NVAPI_OK)                                                                                          \
        {                                                                                                              \
            return ::rhi::NVAPIUtil::handleFail(_res, __FILE__, __LINE__, #x, device);                                 \
        }                                                                                                              \
    }

#define SLANG_RHI_NVAPI_RETURN_NULL_ON_FAIL(x)                                                                         \
    {                                                                                                                  \
        NvAPI_Status _res = x;                                                                                         \
        if (_res != NVAPI_OK)                                                                                          \
        {                                                                                                              \
            ::rhi::NVAPIUtil::handleFail(_res, __FILE__, __LINE__, #x);                                                \
            return nullptr;                                                                                            \
        }                                                                                                              \
    }

#define SLANG_RHI_NVAPI_CHECK(x) SLANG_RHI_NVAPI_CHECK_REPORT(x, nullptr)

#define SLANG_RHI_NVAPI_CHECK_REPORT(x, device)                                                                        \
    {                                                                                                                  \
        NvAPI_Status _res = x;                                                                                         \
        if (_res != NVAPI_OK)                                                                                          \
        {                                                                                                              \
            ::rhi::NVAPIUtil::handleFail(_res, __FILE__, __LINE__, #x, device);                                        \
            SLANG_RHI_ASSERT_FAILURE("NVAPI returned an error");                                                       \
        }                                                                                                              \
    }

} // namespace rhi

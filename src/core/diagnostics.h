#pragma once

#include <slang-rhi.h>

#include <cstdint>
#include <cstddef>

namespace rhi {

class Device;

struct SourceLocation
{
    const char* file = nullptr;
    int line = 0;
};

#define SLANG_RHI_SOURCE_LOCATION()                                                                                    \
    ::rhi::SourceLocation                                                                                              \
    {                                                                                                                  \
        __FILE__, __LINE__                                                                                             \
    }

void formatNativeCallError(
    char* buffer,
    size_t bufferSize,
    const char* call,
    int64_t result,
    const char* resultName,
    const SourceLocation& location,
    const char* detail = nullptr
);

/// Reports a native API call error either through the device's debug message callback
/// or by printing to stderr if no device is available.
void reportNativeCallError(
    Device* device,
    const char* call,
    int64_t result,
    const char* resultName,
    const SourceLocation& location,
    const char* detail = nullptr
);

class ScopedDisableNativeCallError
{
public:
    ScopedDisableNativeCallError();
    ~ScopedDisableNativeCallError();
};

#define SLANG_RHI_DISABLE_NATIVE_CALL_ERROR_SCOPE() ::rhi::ScopedDisableNativeCallError disable_native_call_error__;


} // namespace rhi

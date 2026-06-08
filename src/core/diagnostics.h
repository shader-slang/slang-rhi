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

} // namespace rhi

#define SLANG_RHI_SOURCE_LOCATION()                                                                                    \
    ::rhi::SourceLocation                                                                                              \
    {                                                                                                                  \
        __FILE__, __LINE__                                                                                             \
    }

#pragma once

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang-rhi.h>

namespace rhi {

struct NVAPIUtil
{
    /// Set up NVAPI for use. Must be called before any other function is used.
    static Result initialize();
    /// True if the NVAPI is available, can be called even if initialize fails.
    /// If initialize has not been called will return false
    static bool isAvailable();
};

} // namespace rhi

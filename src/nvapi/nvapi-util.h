#pragma once

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang-rhi.h>

namespace rhi {

struct NVAPIShaderExtension
{
    uint32_t uavSlot = uint32_t(-1);
    uint32_t registerSpace = 0;
    operator bool() const { return uavSlot != uint32_t(-1); }
};

struct NVAPIUtil
{
    /// Set up NVAPI for use. Must be called before any other function is used.
    static Result initialize();

    /// True if the NVAPI is available, can be called even if initialize fails.
    /// If initialize has not been called will return false
    static bool isAvailable();

    static NVAPIShaderExtension findShaderExtension(slang::ProgramLayout* layout);
};

} // namespace rhi

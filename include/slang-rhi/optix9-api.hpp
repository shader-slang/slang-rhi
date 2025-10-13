#pragma once

// Multiple OptiX API versions will be supported by providing separate hpp
// files for each version (e.g., optix7-api.hpp, optix8-api.hpp, optix9-api.hpp).
// To handle multiple OptiX versions, include the appropriate version-specific
// hpp from your cpp implementation files. Each OptiX version requires its
// own cpp file implementation since the APIs may have incompatible differences.
//
// This header provides access to the OptiX 9 API and should only be included
// from cpp files that specifically target OptiX 9.

#include <slang-rhi-config.h>
#include <slang-rhi/cuda-driver-api.h>

#if SLANG_RHI_ENABLE_OPTIX
#define OPTIX_DONT_INCLUDE_CUDA
#include <optix.h>
#include <optix_stubs.h>
#if !(OPTIX_VERSION >= 90000)
#error "OptiX version 9.0 or higher is required. Try reconfigure slang-rhi to fetch the latest OptiX headers."
#endif
#endif

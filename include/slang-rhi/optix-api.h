#pragma once

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

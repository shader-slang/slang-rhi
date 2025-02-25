#pragma once

#include <slang-rhi/cuda-driver-api.h>

#if SLANG_RHI_ENABLE_OPTIX
#define OPTIX_DONT_INCLUDE_CUDA
#include <optix.h>
#include <optix_stubs.h>
#endif

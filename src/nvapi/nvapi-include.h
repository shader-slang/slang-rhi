#pragma once

// A helper that makes the NVAPI available across targets

#if SLANG_RHI_ENABLE_NVAPI
// On windows if we include NVAPI, we must include windows.h first
#include <windows.h>
#include <nvShaderExtnEnums.h>
#include <nvapi.h>

#endif

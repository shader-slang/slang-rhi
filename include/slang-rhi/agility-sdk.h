#pragma once

#include <slang-rhi-config.h>

// -------------------------------------------------------------------------------------------------
// D3D12 Agility SDK
// -------------------------------------------------------------------------------------------------

#if SLANG_RHI_ENABLE_AGILITY_SDK

#include <d3d12.h>

#ifndef SLANG_RHI_AGILITY_SDK_VERSION
#error "SLANG_RHI_AGILITY_SDK_VERSION is not defined"
#endif

#define SLANG_RHI_AGILITY_SDK_PATH ".\\D3D12\\"

// To enable the D3D12 Agility SDK, this macro needs to be added to the main source file of the executable.
#define SLANG_RHI_EXPORT_AGILITY_SDK                                                                                   \
    extern "C"                                                                                                         \
    {                                                                                                                  \
        __declspec(dllexport) extern const UINT D3D12SDKVersion = SLANG_RHI_AGILITY_SDK_VERSION;                       \
    }                                                                                                                  \
    extern "C"                                                                                                         \
    {                                                                                                                  \
        __declspec(dllexport) extern const char* D3D12SDKPath = SLANG_RHI_AGILITY_SDK_PATH;                            \
    }

#else // SLANG_RHI_ENABLE_AGILITY_SDK

#define SLANG_RHI_EXPORT_AGILITY_SDK

#endif // SLANG_RHI_ENABLE_AGILITY_SDK

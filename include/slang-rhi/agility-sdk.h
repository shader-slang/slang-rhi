#pragma once

#include <slang-rhi.h>

// -------------------------------------------------------------------------------------------------
// D3D12 Agility SDK
// -------------------------------------------------------------------------------------------------

#if SLANG_WINDOWS_FAMILY

#include <d3d12.h>

#define SLANG_RHI_AGILITY_SDK_VERSION D3D12_SDK_VERSION
#define SLANG_RHI_AGILITY_SDK_PATH ".\\d3d12\\"

// To enable the D3D12 Agility SDK, this macro needs to be added to the main source file of the executable.
#define SLANG_RHI_EXPORT_AGILITY_SDK                                                                                   \
    extern "C"                                                                                                         \
    {                                                                                                                  \
        __declspec(dllexport) extern const unsigned int D3D12SDKVersion = SLANG_RHI_AGILITY_SDK_VERSION;               \
    }                                                                                                                  \
    extern "C"                                                                                                         \
    {                                                                                                                  \
        __declspec(dllexport) extern const char* D3D12SDKPath = SLANG_RHI_AGILITY_SDK_PATH;                            \
    }

#else
#define SLANG_RHI_EXPORT_AGILITY_SDK
#endif

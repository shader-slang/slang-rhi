#pragma once

#include <slang-rhi.h>

namespace rhi {

struct OptixPreLaunchCallbackDesc
{
    /// Called on the submitting thread immediately before optixLaunch.
    void(SLANG_MCALL* callback)(void* stream, void* userData) = nullptr;
    void* userData = nullptr;
};

} // namespace rhi

/// Install while submissions are quiescent. Pass an empty descriptor to clear the callback.
/// The caller owns userData and must keep it alive until the callback is cleared or the device is destroyed.
/// Returns SLANG_E_NOT_AVAILABLE if the device is not a CUDA device or if OptiX is not available on it.
extern "C" SLANG_RHI_API rhi::Result SLANG_STDCALL
rhiInstallOptixPreLaunchCallback(rhi::IDevice* device, const rhi::OptixPreLaunchCallbackDesc& desc);

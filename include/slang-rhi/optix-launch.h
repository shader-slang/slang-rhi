#pragma once

#include <slang-rhi.h>

namespace rhi {

struct OptixPreLaunchCallbackDesc
{
    /// Called on the submitting thread immediately before optixLaunch.
    void(SLANG_MCALL* callback)(void* stream, void* userData) = nullptr;
    void* userData = nullptr;
};

/// Install while submissions are quiescent. Pass an empty descriptor to clear the callback.
/// The caller owns userData and must keep it alive until the callback is cleared or the device is destroyed.
SLANG_RHI_API Result installOptixPreLaunchCallback(IDevice* device, const OptixPreLaunchCallbackDesc& desc);

} // namespace rhi

#pragma once

#include "geometry.h"

#include <cstdint>

// Shared logo asset, independent of scene composition and rendering. Y is up,
// the base is near Y = 0, and the logo faces +Z. UVs are unused and remain zero.
namespace rhi::logo {

struct MeshRange
{
    uint32_t firstIndex;
    uint32_t indexCount;
    uint32_t materialIndex;
};

// Linear RGB approximations of the orange and cyan logo colors, indexed by
// MeshRange::materialIndex. Surface properties belong to the consuming scene.
inline constexpr math::float3 kColors[] = {
    {1.0f, 0.133f, 0.033f},
    {0.014f, 0.515f, 0.584f},
};

} // namespace rhi::logo

#include "assets/slang-logo-mesh.h"

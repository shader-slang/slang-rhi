#pragma once

#include "../base/logo-scene.h"
#include "settings.h"

#include <algorithm>
#include <cmath>

namespace rhi::particles {

// Area-weighted samples of the visible logo surface, in particle world space.
// Upload once; spawning assigns a persistent target independently of compaction.
inline std::vector<math::float4> createLogoTargets()
{
    struct Triangle
    {
        math::float3 a, b, c;
        float cumulativeArea;
        uint32_t material;
    };
    std::vector<Triangle> triangles;
    float area = 0;
    for (const auto& mesh : logo::kMeshRanges)
        for (uint32_t i = mesh.firstIndex; i < mesh.firstIndex + mesh.indexCount; i += 3)
        {
            auto a = logo::kVertices[logo::kIndices[i]].position;
            auto b = logo::kVertices[logo::kIndices[i + 1]].position;
            auto c = logo::kVertices[logo::kIndices[i + 2]].position;
            float projectedArea = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (projectedArea <= 1e-8f || a.z + b.z + c.z <= 0)
                continue;
            area += projectedArea;
            triangles.push_back({a, b, c, area, mesh.materialIndex});
        }
    if (triangles.empty())
        return {};
    uint32_t seed = 0x51a9u;
    auto random = [&]()
    {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / 16777216.0f;
    };
    std::vector<math::float4> targets;
    targets.reserve(PARTICLE_LOGO_TARGET_COUNT);
    for (uint32_t i = 0; i < PARTICLE_LOGO_TARGET_COUNT; ++i)
    {
        // Stratify triangle selection to preserve even the smaller logo regions.
        float sampleArea = (float(i) + random()) / PARTICLE_LOGO_TARGET_COUNT * area;
        const auto& triangle = *std::lower_bound(
            triangles.begin(),
            triangles.end(),
            sampleArea,
            [](const Triangle& t, float value)
            {
                return t.cumulativeArea < value;
            }
        );
        float u = std::sqrt(random()), v = random();
        auto p = triangle.a * (1 - u) + triangle.b * (u * (1 - v)) + triangle.c * (u * v);
        targets.push_back({p.x * 0.8f, (p.y - 1.58f) * 0.8f, float(triangle.material), 0});
    }
    return targets;
}

} // namespace rhi::particles

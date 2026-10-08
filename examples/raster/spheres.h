#pragma once

#include "../base/math.h"

#include <cstdint>
#include <vector>

namespace rhi::raster {

struct SphereVertex
{
    math::float3 position;
    math::float3 normal;
};

struct SphereMesh
{
    std::vector<SphereVertex> vertices;
    std::vector<uint32_t> indices;
};

inline SphereMesh createSphereMesh()
{
    constexpr uint32_t slices = 48, stacks = 24;
    constexpr float pi = 3.14159265f;
    SphereMesh mesh;
    mesh.vertices.push_back({{0, 1, 0}, {0, 1, 0}});
    for (uint32_t y = 1; y < stacks; ++y)
    {
        float theta = pi * float(y) / stacks;
        for (uint32_t x = 0; x < slices; ++x)
        {
            float phi = 2 * pi * float(x) / slices;
            math::float3 n(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            mesh.vertices.push_back({n, n});
        }
    }
    uint32_t bottom = uint32_t(mesh.vertices.size());
    mesh.vertices.push_back({{0, -1, 0}, {0, -1, 0}});
    // Share the seam and poles: every edge belongs to two non-degenerate triangles.
    for (uint32_t x = 0; x < slices; ++x)
    {
        uint32_t next = (x + 1) % slices;
        mesh.indices.insert(mesh.indices.end(), {0, 1 + next, 1 + x});
        for (uint32_t y = 0; y < stacks - 2; ++y)
        {
            uint32_t a = 1 + y * slices + x, b = 1 + y * slices + next;
            uint32_t c = a + slices, d = b + slices;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
        uint32_t lastRing = 1 + (stacks - 2) * slices;
        mesh.indices.insert(mesh.indices.end(), {bottom, lastRing + x, lastRing + next});
    }
    return mesh;
}

// Three float4s give StructuredBuffer elements the same 48-byte layout on every
// backend. Uniform positive scale lets the unit-sphere normal remain unchanged.
struct SphereInstance
{
    math::float4 translationScale;
    math::float4 baseColorMetallic;
    math::float4 materialParameters; // x: roughness; yzw: unused.
};
static_assert(sizeof(SphereInstance) == 48);

inline constexpr uint32_t kSphereColumns = 6;
inline constexpr uint32_t kSphereRows = 5;
inline constexpr uint32_t kSphereCount = kSphereColumns * kSphereRows;

inline std::vector<SphereInstance> createSphereInstances()
{
    std::vector<SphereInstance> instances;
    for (uint32_t row = 0; row < kSphereRows; ++row)
    {
        for (uint32_t column = 0; column < kSphereColumns; ++column)
        {
            float t = float(column) / (kSphereColumns - 1);
            float metallic = float(row) / (kSphereRows - 1);
            float x = column < 3 ? -4.6f + 1.05f * column : 2.5f + 1.05f * (column - 3);
            constexpr float radius = 0.38f;
            math::float3 color = math::float3(1.0f, 0.18f, 0.05f) * (1 - t) + math::float3(0.02f, 0.5f, 0.6f) * t;
            instances.push_back({
                {x, radius - 0.03f, 2.6f - 1.3f * row, radius},
                math::float4(color, metallic),
                {0.08f + 0.92f * t, 0, 0, 0},
            });
        }
    }
    return instances;
}

} // namespace rhi::raster

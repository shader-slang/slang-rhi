#pragma once

#include "math.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace rhi::geometry {

// Shared CPU/GPU vertex layout for all example scene geometry.
struct Vertex
{
    math::float3 position;
    math::float3 normal;
    math::float2 uv = {};
};
static_assert(sizeof(Vertex) == 32);

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    Mesh() = default;

    Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices)
        : vertices(std::move(vertices))
        , indices(std::move(indices))
    {
    }

    uint32_t vertexCount() const { return (uint32_t)(vertices.size()); }
    uint32_t indexCount() const { return (uint32_t)(indices.size()); }

    static Mesh createQuad(math::float2 size = math::float2(1))
    {
        std::vector<Vertex> vertices = {
            // position, normal, uv
            {{-0.5, 0, -0.5}, {0, 1, 0}, {0, 0}},
            {{+0.5, 0, -0.5}, {0, 1, 0}, {1, 0}},
            {{-0.5, 0, +0.5}, {0, 1, 0}, {0, 1}},
            {{+0.5, 0, +0.5}, {0, 1, 0}, {1, 1}},
        };
        for (auto& vertex : vertices)
        {
            vertex.position *= math::float3(size.x, 0.f, size.y);
        }
        std::vector<uint32_t> indices = {
            2,
            1,
            0,
            1,
            2,
            3,
        };
        return Mesh(vertices, indices);
    }

    static Mesh createCube(math::float3 size = math::float3(1))
    {
        std::vector<Vertex> vertices = {
            // position, normal, uv
            // left
            {{-0.5, -0.5, -0.5}, {0, -1, 0}, {0, 0}},
            {{-0.5, -0.5, +0.5}, {0, -1, 0}, {1, 0}},
            {{+0.5, -0.5, +0.5}, {0, -1, 0}, {1, 1}},
            {{+0.5, -0.5, -0.5}, {0, -1, 0}, {0, 1}},
            // right
            {{-0.5, +0.5, +0.5}, {0, +1, 0}, {0, 0}},
            {{-0.5, +0.5, -0.5}, {0, +1, 0}, {1, 0}},
            {{+0.5, +0.5, -0.5}, {0, +1, 0}, {1, 1}},
            {{+0.5, +0.5, +0.5}, {0, +1, 0}, {0, 1}},
            // back
            {{-0.5, +0.5, -0.5}, {0, 0, -1}, {0, 0}},
            {{-0.5, -0.5, -0.5}, {0, 0, -1}, {1, 0}},
            {{+0.5, -0.5, -0.5}, {0, 0, -1}, {1, 1}},
            {{+0.5, +0.5, -0.5}, {0, 0, -1}, {0, 1}},
            // front
            {{+0.5, +0.5, +0.5}, {0, 0, +1}, {0, 0}},
            {{+0.5, -0.5, +0.5}, {0, 0, +1}, {1, 0}},
            {{-0.5, -0.5, +0.5}, {0, 0, +1}, {1, 1}},
            {{-0.5, +0.5, +0.5}, {0, 0, +1}, {0, 1}},
            // bottom
            {{-0.5, +0.5, +0.5}, {-1, 0, 0}, {0, 0}},
            {{-0.5, -0.5, +0.5}, {-1, 0, 0}, {1, 0}},
            {{-0.5, -0.5, -0.5}, {-1, 0, 0}, {1, 1}},
            {{-0.5, +0.5, -0.5}, {-1, 0, 0}, {0, 1}},
            // top
            {{+0.5, +0.5, -0.5}, {+1, 0, 0}, {0, 0}},
            {{+0.5, -0.5, -0.5}, {+1, 0, 0}, {1, 0}},
            {{+0.5, -0.5, +0.5}, {+1, 0, 0}, {1, 1}},
            {{+0.5, +0.5, +0.5}, {+1, 0, 0}, {0, 1}},
        };
        for (auto& vertex : vertices)
        {
            vertex.position *= size;
        }
        std::vector<uint32_t> indices = {
            0,  2,  1,  0,  3,  2,  4,  6,  5,  4,  7,  6,  8,  10, 9,  8,  11, 10,
            12, 14, 13, 12, 15, 14, 16, 18, 17, 16, 19, 18, 20, 22, 21, 20, 23, 22,
        };
        return Mesh(vertices, indices);
    }

    // Unit sphere with longitude U and north-to-south latitude V in [0, 1].
    // Duplicate the seam and give each cap triangle its own pole UV so texture
    // interpolation stays within one longitude segment, including at the poles.
    static Mesh createSphere(uint32_t slices = 48, uint32_t stacks = 24)
    {
        constexpr float pi = 3.14159265f;
        Mesh mesh;
        if (slices < 3 || stacks < 2)
            return mesh;
        for (uint32_t y = 1; y < stacks; ++y)
        {
            float v = float(y) / stacks;
            float theta = pi * v;
            for (uint32_t x = 0; x <= slices; ++x)
            {
                float u = float(x) / slices;
                // Reuse the exact position at the seam to avoid a floating-point crack.
                float phi = x == slices ? 0.0f : 2 * pi * u;
                math::float3 n(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
                mesh.vertices.push_back({n, n, {u, v}});
            }
        }
        uint32_t top = uint32_t(mesh.vertices.size());
        for (uint32_t x = 0; x < slices; ++x)
            mesh.vertices.push_back({{0, 1, 0}, {0, 1, 0}, {(float(x) + 0.5f) / slices, 0}});
        uint32_t bottom = uint32_t(mesh.vertices.size());
        for (uint32_t x = 0; x < slices; ++x)
            mesh.vertices.push_back({{0, -1, 0}, {0, -1, 0}, {(float(x) + 0.5f) / slices, 1}});

        for (uint32_t x = 0; x < slices; ++x)
        {
            mesh.indices.insert(mesh.indices.end(), {top + x, x + 1, x});
            for (uint32_t y = 0; y < stacks - 2; ++y)
            {
                uint32_t a = y * (slices + 1) + x, b = a + 1;
                uint32_t c = a + slices + 1, d = c + 1;
                mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
            }
            uint32_t lastRing = (stacks - 2) * (slices + 1);
            mesh.indices.insert(mesh.indices.end(), {bottom + x, lastRing + x, lastRing + x + 1});
        }
        return mesh;
    }
};

} // namespace rhi::geometry

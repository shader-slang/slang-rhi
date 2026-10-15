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
};

} // namespace rhi::geometry

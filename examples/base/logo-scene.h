#pragma once

#include "math.h"

#include <cstdint>
#include <vector>

// Renderer-independent indexed meshes and materials. Positions are in world
// space, Y is up, and the logo faces +Z. A ray tracer can use these same arrays
// as triangle build inputs without depending on the raster example.
namespace rhi::logo {

struct Vertex
{
    math::float3 position;
    math::float3 normal;
};

struct Material
{
    math::float3 baseColor;
    float metallic;
    float roughness;
};

struct MeshRange
{
    uint32_t firstIndex;
    uint32_t indexCount;
    uint32_t materialIndex;
};

} // namespace rhi::logo

#include "assets/slang-logo-mesh.h"

namespace rhi::logo {

struct Scene
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshRange> meshes;
    std::vector<Material> materials;
};

inline Scene createScene()
{
    Scene scene;
    scene.vertices.assign(std::begin(kVertices), std::end(kVertices));
    scene.indices.assign(std::begin(kIndices), std::end(kIndices));
    scene.meshes.assign(std::begin(kMeshRanges), std::end(kMeshRanges));
    // Linear RGB approximations of the orange and cyan logo colors.
    scene.materials = {
        {{1.0f, 0.133f, 0.033f}, 0.55f, 0.28f},
        {{0.014f, 0.515f, 0.584f}, 0.55f, 0.28f},
        {{0.075f, 0.09f, 0.12f}, 0.0f, 0.65f},
    };
    uint32_t base = uint32_t(scene.vertices.size());
    scene.vertices.insert(
        scene.vertices.end(),
        {
            {{-100, -0.03f, -100}, {0, 1, 0}},
            {{-100, -0.03f, +100}, {0, 1, 0}},
            {{+100, -0.03f, +100}, {0, 1, 0}},
            {{+100, -0.03f, -100}, {0, 1, 0}},
        }
    );
    scene.meshes.push_back({uint32_t(scene.indices.size()), 6, 2});
    for (uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u})
        scene.indices.push_back(base + index);
    return scene;
}

} // namespace rhi::logo

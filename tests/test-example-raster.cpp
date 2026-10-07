#include "testing.h"
#include "../examples/raster/renderer.h"

#include <cmath>
#include <map>

using namespace rhi;
using namespace rhi::testing;

TEST_CASE("example-raster-geometry")
{
    auto scene = logo::createScene();
    REQUIRE(scene.meshes.size() == 3);
    for (const auto& vertex : scene.vertices)
    {
        CHECK(math::length(vertex.normal) == doctest::Approx(1).epsilon(1e-5));
        CHECK(std::isfinite(vertex.position.x));
        CHECK(std::isfinite(vertex.position.y));
        CHECK(std::isfinite(vertex.position.z));
    }
    for (const auto& mesh : scene.meshes)
    {
        REQUIRE(mesh.firstIndex + mesh.indexCount <= scene.indices.size());
        REQUIRE(mesh.indexCount % 3 == 0);
        REQUIRE(mesh.materialIndex < scene.materials.size());
        for (uint32_t i = mesh.firstIndex; i < mesh.firstIndex + mesh.indexCount; i += 3)
        {
            for (uint32_t j = 0; j < 3; ++j)
                REQUIRE(scene.indices[i + j] < scene.vertices.size());
            const auto& a = scene.vertices[scene.indices[i]];
            const auto& b = scene.vertices[scene.indices[i + 1]];
            const auto& c = scene.vertices[scene.indices[i + 2]];
            auto normal = math::cross(b.position - a.position, c.position - a.position);
            CHECK(math::length(normal) > 1e-9f);
            CHECK(math::dot(normal, a.normal + b.normal + c.normal) > 0);
        }
    }
}

TEST_CASE("example-raster-smoothing")
{
    auto scene = logo::createScene();
    struct Edge
    {
        uint32_t a, b;
        math::float3 normal;
    };
    size_t curvedEdges = 0, creases = 0;
    for (size_t meshIndex = 0; meshIndex < 2; ++meshIndex)
    {
        const auto& mesh = scene.meshes[meshIndex];
        std::map<std::array<float, 6>, std::vector<Edge>> edges;
        for (uint32_t i = mesh.firstIndex; i < mesh.firstIndex + mesh.indexCount; i += 3)
        {
            const auto& a = scene.vertices[scene.indices[i]].position;
            const auto& b = scene.vertices[scene.indices[i + 1]].position;
            const auto& c = scene.vertices[scene.indices[i + 2]].position;
            auto normal = math::normalize(math::cross(b - a, c - a));
            for (uint32_t corner = 0; corner < 3; ++corner)
            {
                uint32_t first = scene.indices[i + corner], second = scene.indices[i + (corner + 1) % 3];
                const auto& p = scene.vertices[first].position;
                const auto& q = scene.vertices[second].position;
                std::array<float, 3> start = {p.x, p.y, p.z}, end = {q.x, q.y, q.z};
                if (end < start)
                {
                    std::swap(start, end);
                    std::swap(first, second);
                }
                edges[{start[0], start[1], start[2], end[0], end[1], end[2]}].push_back({first, second, normal});
            }
        }
        for (const auto& entry : edges)
        {
            const auto& pair = entry.second;
            REQUIRE(pair.size() == 2); // Closed geometry, even where normals split.
            float cosine = math::dot(pair[0].normal, pair[1].normal);
            float differenceA = math::length(scene.vertices[pair[0].a].normal - scene.vertices[pair[1].a].normal);
            float differenceB = math::length(scene.vertices[pair[0].b].normal - scene.vertices[pair[1].b].normal);
            if (cosine > 0.8661f && cosine < 0.9999f)
            {
                ++curvedEdges;
                CHECK(differenceA < 1e-5f);
                CHECK(differenceB < 1e-5f);
            }
            if (cosine < 0.8f && differenceA > 0.2f && differenceB > 0.2f)
                ++creases;
        }
    }
    CHECK(curvedEdges > 100);
    CHECK(creases > 100);
}

GPU_TEST_CASE("example-raster-render", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    if (!device->hasFeature(Feature::Rasterization))
        SKIP("Rasterization is not supported");
    raster::Programs programs;
    REQUIRE_CALL(
        loadProgram(device, "test-example-raster", {"shadowVertex", "shadowFragment"}, programs.shadow.writeRef())
    );
    REQUIRE_CALL(
        loadProgram(device, "test-example-raster", {"sceneVertex", "sceneFragment"}, programs.scene.writeRef())
    );
    REQUIRE_CALL(loadProgram(device, "test-example-raster-post", "bloomMain", programs.bloom.writeRef()));
    REQUIRE_CALL(loadProgram(
        device,
        "test-example-raster-post",
        {"fullscreenVertex", "toneMapFragment"},
        programs.toneMap.writeRef()
    ));
    raster::Renderer renderer;
    Result initResult = renderer.init(device, Format::RGBA8Unorm, programs);
    if (initResult == SLANG_E_NOT_AVAILABLE)
        SKIP("Required HDR or depth texture format support is unavailable");
    REQUIRE_CALL(initResult);
    auto queue = device->getQueue(QueueType::Graphics);

    auto render = [&](uint32_t width, uint32_t height, const raster::Settings& settings)
    {
        REQUIRE_CALL(queue->waitOnHost());
        REQUIRE_CALL(renderer.resize(width, height));
        TextureDesc desc = {};
        desc.size = {width, height, 1};
        desc.format = Format::RGBA8Unorm;
        desc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
        auto output = device->createTexture(desc);
        REQUIRE(output);
        auto encoder = queue->createCommandEncoder();
        REQUIRE_CALL(renderer.render(encoder, output, settings));
        ComPtr<ICommandBuffer> commands;
        REQUIRE_CALL(encoder->finish(commands.writeRef()));
        REQUIRE_CALL(queue->submit(commands));
        REQUIRE_CALL(queue->waitOnHost());
        ComPtr<ISlangBlob> pixels;
        SubresourceLayout layout;
        REQUIRE_CALL(device->readTexture(output, 0, 0, pixels.writeRef(), &layout));
        std::vector<uint8_t> result(width * height * 4);
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(
                result.data() + y * width * 4,
                static_cast<const uint8_t*>(pixels->getBufferPointer()) + y * layout.rowPitch,
                width * 4
            );
        return result;
    };

    // Odd dimensions exercise the half-resolution bloom and dispatch edge guards.
    raster::Settings settings;
    settings.bloom = false;
    auto plain = render(257, 193, settings);
    settings.bloom = true;
    auto bloom = render(257, 193, settings);
    size_t orange = 0, cyan = 0, brighter = 0;
    for (size_t i = 0; i < plain.size(); i += 4)
    {
        orange += plain[i] > plain[i + 1] + 20 && plain[i] > plain[i + 2] + 20;
        cyan += plain[i + 1] > plain[i] + 20 && plain[i + 2] > plain[i] + 20;
        CHECK(plain[i + 3] == 255);
        for (size_t j = 0; j < 3; ++j)
        {
            CHECK(int(bloom[i + j]) >= int(plain[i + j]) - 1);
            brighter += bloom[i + j] > plain[i + j];
        }
    }
    CHECK(orange > 100);
    CHECK(cyan > 100);
    CHECK(brighter > 20);
    settings.exposure = 1;
    auto exposed = render(257, 193, settings);
    uint64_t bloomSum = 0, exposedSum = 0;
    for (size_t i = 0; i < bloom.size(); ++i)
    {
        bloomSum += bloom[i];
        exposedSum += exposed[i];
    }
    CHECK(exposedSum > bloomSum);
    settings.lightYaw += 1.5f;
    auto movedLight = render(257, 193, settings);
    CHECK(movedLight != exposed);
    auto tiny = render(1, 1, settings);
    CHECK(tiny.size() == 4);
    CHECK(tiny[3] == 255);
}

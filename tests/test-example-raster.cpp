#include "testing.h"
#include "../examples/raster/renderer.h"

#include <cmath>
#include <map>

using namespace rhi;
using namespace rhi::testing;

TEST_CASE("example-raster-spheres")
{
    auto mesh = raster::createSphereMesh();
    REQUIRE(mesh.indices.size() % 3 == 0);
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> edges;
    for (const auto& vertex : mesh.vertices)
    {
        CHECK(math::length(vertex.position) == doctest::Approx(1).epsilon(1e-5));
        CHECK(math::length(vertex.normal - vertex.position) < 1e-6f);
    }
    for (size_t i = 0; i < mesh.indices.size(); i += 3)
    {
        for (uint32_t corner = 0; corner < 3; ++corner)
        {
            uint32_t a = mesh.indices[i + corner], b = mesh.indices[i + (corner + 1) % 3];
            REQUIRE(a < mesh.vertices.size());
            REQUIRE(b < mesh.vertices.size());
            ++edges[std::minmax(a, b)];
        }
        auto a = mesh.vertices[mesh.indices[i]].position;
        auto b = mesh.vertices[mesh.indices[i + 1]].position;
        auto c = mesh.vertices[mesh.indices[i + 2]].position;
        auto normal = math::cross(b - a, c - a);
        CHECK(math::length(normal) > 1e-6f);
        CHECK(math::dot(normal, a + b + c) > 0);
    }
    for (const auto& edge : edges)
        CHECK(edge.second == 2);

    auto instances = raster::createSphereInstances();
    REQUIRE(instances.size() == raster::kSphereCount);
    for (size_t i = 0; i < instances.size(); ++i)
    {
        const auto& instance = instances[i];
        CHECK(instance.translationScale.w > 0);
        CHECK(instance.translationScale.y - instance.translationScale.w == doctest::Approx(-0.03f));
        CHECK(instance.baseColorMetallic.w >= 0);
        CHECK(instance.baseColorMetallic.w <= 1);
        CHECK(instance.materialParameters.x >= 0.08f);
        CHECK(instance.materialParameters.x <= 1);
        for (size_t j = i + 1; j < instances.size(); ++j)
        {
            auto delta = instance.translationScale - instances[j].translationScale;
            CHECK(
                math::length(math::float3(delta.x, delta.y, delta.z)) >
                instance.translationScale.w + instances[j].translationScale.w
            );
        }
    }
}

GPU_TEST_CASE("example-raster-environment", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    FormatSupport support = FormatSupport::None;
    REQUIRE_CALL(device->getFormatSupport(Format::RGBA16Float, &support));
    if (!is_set(support, FormatSupport::ShaderSample | FormatSupport::ShaderUavStore))
        SKIP("Required HDR texture support is unavailable");
    ComPtr<IShaderProgram> filter, brdf, inspect;
    REQUIRE_CALL(loadProgram(device, "test-example-raster-environment", "environmentMain", filter.writeRef()));
    REQUIRE_CALL(loadProgram(device, "test-example-raster-environment", "brdfMain", brdf.writeRef()));
    REQUIRE_CALL(loadProgram(device, "test-example-raster-environment", "inspectEnvironment", inspect.writeRef()));
    raster::Environment environment;
    REQUIRE_CALL(environment.init(device, filter, brdf));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = inspect;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    REQUIRE(pipeline);
    SamplerDesc samplerDesc = {};
    samplerDesc.addressU = samplerDesc.addressV = samplerDesc.addressW = TextureAddressingMode::ClampToEdge;
    auto sampler = device->createSampler(samplerDesc);
    REQUIRE(sampler);
    BufferDesc bufferDesc = {};
    bufferDesc.size = 128 * 12 * sizeof(math::float4);
    bufferDesc.elementSize = sizeof(math::float4);
    bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    auto buffer = device->createBuffer(bufferDesc);
    REQUIRE(buffer);
    auto queue = device->getQueue(QueueType::Graphics);
    for (uint32_t preset = 0; preset < 2; ++preset)
    {
        CAPTURE(preset);
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        ShaderCursor cursor(pass->bindPipeline(pipeline));
        REQUIRE_CALL(cursor["probeSpecular"].setBinding(environment.specular[preset]));
        REQUIRE_CALL(cursor["probeDiffuse"].setBinding(environment.diffuse[preset]));
        REQUIRE_CALL(cursor["probeBrdf"].setBinding(environment.brdf));
        REQUIRE_CALL(cursor["probeSampler"].setBinding(sampler));
        REQUIRE_CALL(cursor["probeResults"].setBinding(buffer));
        REQUIRE_CALL(cursor["probePreset"].setData(preset));
        pass->dispatchCompute(4, 1, 1);
        pass->end();
        ComPtr<ICommandBuffer> commands;
        REQUIRE_CALL(encoder->finish(commands.writeRef()));
        REQUIRE_CALL(queue->submit(commands));
        REQUIRE_CALL(queue->waitOnHost());
        ComPtr<ISlangBlob> data;
        REQUIRE_CALL(device->readBuffer(buffer, 0, bufferDesc.size, data.writeRef()));
        const auto* values = static_cast<const math::float4*>(data->getBufferPointer());
        double error = 0;
        for (uint32_t i = 0; i < 128; ++i)
        {
            CAPTURE(i);
            const auto* samples = values + i * 12;
            for (uint32_t j = 0; j < 12; ++j)
            {
                for (uint32_t c = 0; c < 3; ++c)
                {
                    CHECK(std::isfinite(samples[j][c]));
                    CHECK(samples[j][c] >= 0);
                    CHECK(samples[j][c] < 10);
                }
                CHECK(samples[j].w == doctest::Approx(1).epsilon(0.001));
            }
            for (uint32_t c = 0; c < 3; ++c)
            {
                float difference = std::abs(samples[0][c] - samples[9][c]);
                CHECK(difference < 0.2f);
                error += difference;
            }
            // White-furnace energy bound for the integrated single-scattering BRDF.
            CHECK(samples[11].x + samples[11].y <= 1.02f);
        }
        CHECK(error / (128 * 3) < 0.02);
    }
}

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
    REQUIRE_CALL(
        loadProgram(device, "test-example-raster", {"sphereVertex", "sceneFragment"}, programs.spheres.writeRef())
    );
    REQUIRE_CALL(loadProgram(
        device,
        "test-example-raster",
        {"sphereShadowVertex", "shadowFragment"},
        programs.sphereShadow.writeRef()
    ));
    REQUIRE_CALL(loadProgram(device, "test-example-raster-post", "bloomMain", programs.bloom.writeRef()));
    REQUIRE_CALL(loadProgram(
        device,
        "test-example-raster-post",
        {"fullscreenVertex", "toneMapFragment"},
        programs.toneMap.writeRef()
    ));
    REQUIRE_CALL(loadProgram(
        device,
        "test-example-raster",
        {"backgroundVertex", "backgroundFragment"},
        programs.background.writeRef()
    ));
    REQUIRE_CALL(
        loadProgram(device, "test-example-raster-environment", "environmentMain", programs.environment.writeRef())
    );
    REQUIRE_CALL(
        loadProgram(device, "test-example-raster-environment", "brdfMain", programs.environmentBrdf.writeRef())
    );
    raster::Renderer renderer;
    Result initResult = renderer.init(device, Format::RGBA8Unorm, programs);
    if (initResult == SLANG_E_NOT_AVAILABLE)
        SKIP("Required HDR or depth texture format support is unavailable");
    REQUIRE_CALL(initResult);
    auto queue = device->getQueue(QueueType::Graphics);

    auto render = [&](uint32_t width, uint32_t height, const raster::Settings& settings, bool resizeTargets = true)
    {
        REQUIRE_CALL(queue->waitOnHost());
        if (resizeTargets)
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
    // Keep sky visible at the upper-left corner for the background checks below.
    settings.pitch = 0.18f;
    SUBCASE("single sample")
    {
        settings.msaa = false;
    }
    SUBCASE("4x MSAA")
    {
        if (!renderer.supportsMsaa())
            SKIP("4x HDR color/depth attachments are unavailable");
        settings.msaa = true;
    }
    settings.bloom = false;
    auto plain = render(257, 193, settings);
    auto checkRepeatedImage = [&](const std::vector<uint8_t>& actual)
    {
        REQUIRE(actual.size() == plain.size());
        // On D3D11, MSAA resolve/shading roundoff can change a few channels by
        // one 8-bit step. Keep the single-sample comparison exact.
        int tolerance = settings.msaa ? 1 : 0;
        CHECK(
            std::equal(
                plain.begin(),
                plain.end(),
                actual.begin(),
                [=](uint8_t a, uint8_t b)
                {
                    return std::abs(int(a) - int(b)) <= tolerance;
                }
            )
        );
    };
    raster::Settings withoutSpheres = settings;
    withoutSpheres.spheres = false;
    CHECK(render(257, 193, withoutSpheres, false) != plain);
    checkRepeatedImage(render(257, 193, settings, false));
    if (renderer.supportsMsaa())
    {
        // Switch pipelines and attachments without recreating resources, as the
        // interactive toggle does. Returning to the original mode preserves the image.
        raster::Settings toggled = settings;
        toggled.msaa = !settings.msaa;
        auto otherSamples = render(257, 193, toggled, false);
        CHECK(otherSamples != plain);
        checkRepeatedImage(render(257, 193, settings, false));
        CHECK(std::equal(plain.begin(), plain.begin() + 4, otherSamples.begin()));
    }
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

    // Environment lighting affects geometry even with the background held fixed.
    settings.bloom = false;
    settings.environmentLighting = false;
    auto directOnly = render(257, 193, settings);
    size_t litPixels = 0;
    for (size_t i = 0; i < plain.size(); i += 4)
    {
        for (size_t j = 0; j < 3; ++j)
            CHECK(int(plain[i + j]) >= int(directOnly[i + j]) - 1);
        litPixels += plain[i] > directOnly[i] + 3 || plain[i + 1] > directOnly[i + 1] + 3;
    }
    CHECK(litPixels > 1000);
    // The upper-left pixel is background, and must not change with the IBL toggle.
    CHECK(std::equal(plain.begin(), plain.begin() + 4, directOnly.begin()));
    settings.environmentLighting = true;
    settings.environmentRotation = 1.4f;
    auto rotated = render(257, 193, settings);
    CHECK(rotated != plain);
    settings.environmentRotation = 0;
    settings.roughness = 0.9f;
    CHECK(render(257, 193, settings) != plain);
    settings.roughness = raster::Settings{}.roughness;
    settings.environmentPreset = 1;
    auto outdoor = render(257, 193, settings);
    CHECK(outdoor != plain);
    CHECK(!std::equal(plain.begin(), plain.begin() + 3, outdoor.begin()));
    settings.environmentPreset = 0;
    // Switching back reuses the original maps, including after target recreation.
    checkRepeatedImage(render(257, 193, settings));

    settings.bloom = true;
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
    settings.environmentPreset = 1;
    tiny = render(1, 1, settings);
    CHECK(tiny[3] == 255);

    // Check every instance at its projected center. This catches missing instances,
    // incorrect structured-buffer strides, and bad per-instance translations.
    settings = {};
    settings.msaa = withoutSpheres.msaa;
    settings.bloom = false;
    settings.pitch = 0.8f;
    constexpr uint32_t width = 641, height = 481;
    auto spheres = render(width, height, settings);
    settings.spheres = false;
    auto ground = render(width, height, settings, false);
    auto target = math::float3(0, 1.58f, 0);
    auto eye = target + raster::orbitDirection(settings.yaw, settings.pitch) * settings.distance;
    auto vp = math::mul(raster::perspective(float(width) / height), raster::lookAt(eye, target));
    for (const auto& instance : raster::createSphereInstances())
    {
        const auto& transform = instance.translationScale;
        auto clip = math::mul(vp, math::float4(transform.x, transform.y, transform.z, 1));
        int x = int((clip.x / clip.w * 0.5f + 0.5f) * width);
        int y = int((0.5f - clip.y / clip.w * 0.5f) * height);
        CAPTURE(x);
        CAPTURE(y);
        REQUIRE(x >= 0);
        REQUIRE(x < int(width));
        REQUIRE(y >= 0);
        REQUIRE(y < int(height));
        size_t offset = (y * width + x) * 4;
        int difference = 0;
        for (size_t c = 0; c < 3; ++c)
            difference += std::abs(int(spheres[offset + c]) - int(ground[offset + c]));
        CHECK(difference > 15);
    }
}

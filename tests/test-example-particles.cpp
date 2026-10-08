#include "testing.h"
#include "../examples/particles/renderer.h"
#include "../examples/particles/simulation-clock.h"

#include <cmath>
#include <cstring>

using namespace rhi;
using namespace rhi::testing;

TEST_CASE("example-particles-clock")
{
    particles::SimulationClock a, b;
    CHECK(a.update(10, true) == 0);
    CHECK(b.update(10, true) == 0);
    for (int i = 1; i <= 100; ++i)
    {
        double time = 10 + i / 60.0;
        CHECK(a.update(time, true) == 2);
        CHECK(b.update(time, true) == 2);
    }
    CHECK(a.update(100, true) == 8);
    CHECK(a.update(101, false) == 0);
    CHECK(a.update(102, false) == 0);
    CHECK(a.update(102 + 1.0 / 120, true) == 1);
    a.reset();
    CHECK(a.update(102 + 2.0 / 120, true) == 1);
}

GPU_TEST_CASE("example-particles", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    particles::Programs programs;
    REQUIRE_CALL(loadProgram(device, "test-example-particles", "simulateMain", programs.simulate.writeRef()));
    REQUIRE_CALL(loadProgram(device, "test-example-particles", "spawnMain", programs.spawn.writeRef()));
    REQUIRE_CALL(loadProgram(device, "test-example-particles", "finalizeMain", programs.finalize.writeRef()));
    REQUIRE_CALL(loadProgram(
        device,
        "test-example-particles",
        {"particleVertex", "particleFragment"},
        programs.particles.writeRef()
    ));
    REQUIRE_CALL(
        loadProgram(device, "test-example-particles", {"logoVertex", "logoFragment"}, programs.logo.writeRef())
    );
    REQUIRE_CALL(loadProgram(device, "test-example-raster-post", "bloomMain", programs.bloom.writeRef()));
    REQUIRE_CALL(loadProgram(
        device,
        "test-example-raster-post",
        {"fullscreenVertex", "toneMapFragment"},
        programs.toneMap.writeRef()
    ));
    particles::Renderer renderer;
    constexpr uint32_t capacity = 4097;
    Result result = renderer.init(device, Format::RGBA8Unorm, programs, capacity);
    if (result == SLANG_E_NOT_AVAILABLE)
        SKIP("Required HDR format support is unavailable");
    REQUIRE_CALL(result);
    auto queue = device->getQueue(QueueType::Graphics);
    GpuProfiler profiler;
    REQUIRE_CALL(profiler.init(device));
    auto step = [&](uint32_t spawn, float dt)
    {
        auto encoder = queue->createCommandEncoder();
        REQUIRE_CALL(profiler.beginFrame());
        particles::SimulationParams params;
        params.spawnCount = spawn;
        params.dt = dt;
        REQUIRE_CALL(renderer.step(encoder, params, &profiler));
        REQUIRE_CALL(profiler.endFrame());
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        REQUIRE_CALL(profiler.poll());
    };
    auto count = [&]()
    {
        uint32_t value = 0;
        REQUIRE_CALL(device->readBuffer(renderer.countBuffer(), 0, sizeof(value), &value));
        uint32_t args[4];
        REQUIRE_CALL(device->readBuffer(renderer.argumentBuffer(), 0, sizeof(args), args));
        CHECK(args[0] == 6);
        CHECK(args[1] == value);
        CHECK(args[2] == 0);
        CHECK(args[3] == 0);
        return value;
    };
    step(0, 1.0f / 120);
    CHECK(count() == 0);
    step(200, 1.0f / 120);
    CHECK(count() == 200);
    step(capacity, 1.0f / 120);
    CHECK(count() == capacity);
    step(capacity, 1.0f / 120);
    CHECK(count() == capacity);
    step(0, 4.0f);
    uint32_t survivors = count();
    CHECK(survivors > 0);
    CHECK(survivors < capacity);
    std::vector<particles::Particle> data(survivors);
    REQUIRE_CALL(device->readBuffer(renderer.particleBuffer(), 0, data.size() * sizeof(data[0]), data.data()));
    for (const auto& p : data)
    {
        CHECK(std::isfinite(p.positionAge.x));
        CHECK(std::isfinite(p.positionAge.y));
        CHECK(p.positionAge.w < p.velocityLife.w);
        CHECK(p.positionAge.w >= 4);
    }
    step(0, 10);
    CHECK(count() == 0);

    auto reset = [&]()
    {
        auto encoder = queue->createCommandEncoder();
        renderer.reset(encoder);
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
    };
    reset();
    step(capacity, 1.0f / 120);
    std::vector<particles::Particle> first(capacity), second(capacity);
    REQUIRE_CALL(device->readBuffer(renderer.particleBuffer(), 0, first.size() * sizeof(first[0]), first.data()));
    reset();
    step(capacity, 1.0f / 120);
    REQUIRE_CALL(device->readBuffer(renderer.particleBuffer(), 0, second.size() * sizeof(second[0]), second.data()));
    CHECK(std::memcmp(first.data(), second.data(), first.size() * sizeof(first[0])) == 0);
    reset();
    step(capacity / 2, 1.0f / 120);
    step(0, 0.2f); // Partially occupied buffers also exercise direct-draw clipping.

    auto render = [&](bool indirect, bool bloom, uint32_t width = 257, uint32_t height = 193)
    {
        REQUIRE_CALL(renderer.resize(width, height));
        renderer.setIndirect(indirect);
        TextureDesc desc = {};
        desc.size = {width, height, 1};
        desc.format = Format::RGBA8Unorm;
        desc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
        auto output = device->createTexture(desc);
        REQUIRE(output);
        auto encoder = queue->createCommandEncoder();
        REQUIRE_CALL(profiler.beginFrame());
        REQUIRE_CALL(renderer.render(encoder, output, bloom, &profiler));
        REQUIRE_CALL(profiler.endFrame());
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        REQUIRE_CALL(profiler.poll());
        ComPtr<ISlangBlob> blob;
        SubresourceLayout layout;
        REQUIRE_CALL(device->readTexture(output, 0, 0, blob.writeRef(), &layout));
        std::vector<uint8_t> pixels(width * height * 4);
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(
                pixels.data() + y * width * 4,
                static_cast<const uint8_t*>(blob->getBufferPointer()) + y * layout.rowPitch,
                width * 4
            );
        return pixels;
    };
    // Resolve the small sprites over several pixels so bloom has real highlights.
    auto indirect = render(true, false, 769, 577);
    CHECK(indirect == render(false, false, 769, 577));
    auto bloom = render(true, true, 769, 577);
    uint64_t brightness = 0, bloomBrightness = 0;
    for (size_t i = 0; i < indirect.size(); ++i)
        if (i % 4 != 3)
        {
            brightness += indirect[i];
            bloomBrightness += bloom[i];
        }
    CHECK(bloomBrightness > brightness);
    step(0, 10);
    auto empty = render(true, false, 769, 577);
    CHECK(empty != indirect);
    CHECK(render(true, true, 1, 1).size() == 4);
    if (profiler.supported())
    {
        REQUIRE(!profiler.samples().empty());
        for (const auto& sample : profiler.samples())
        {
            CHECK(std::isfinite(sample.milliseconds));
            CHECK(sample.milliseconds >= 0);
        }
    }
}

GPU_TEST_CASE("example-gpu-profiler-ring", ALL)
{
    GpuProfiler profiler;
    REQUIRE_CALL(profiler.init(device));
    if (!profiler.supported())
        SKIP("Timestamp queries are unavailable");
    auto queue = device->getQueue(QueueType::Graphics);
    std::vector<ComPtr<ICommandBuffer>> commands;
    // Leave recorded work unsubmitted to deterministically fill the ring.
    for (uint32_t i = 0; i < GpuProfiler::kFrameCount; ++i)
    {
        REQUIRE_CALL(profiler.beginFrame());
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto region = profiler.beginRegion(pass, "Test region");
        CHECK(region != GpuProfiler::kInvalidRegion);
        profiler.endRegion(pass, region);
        pass->end();
        REQUIRE_CALL(profiler.endFrame());
        commands.push_back(encoder->finish());
    }
    REQUIRE_CALL(profiler.beginFrame());
    CHECK(profiler.droppedFrames() == 1);
    CHECK(profiler.samples().empty());
    REQUIRE_CALL(profiler.endFrame());
    for (auto& command : commands)
        REQUIRE_CALL(queue->submit(command));
    REQUIRE_CALL(queue->waitOnHost());
    REQUIRE_CALL(profiler.poll());
    REQUIRE(profiler.samples().size() == 1);
    CHECK(profiler.samples()[0].name == "Test region");
    CHECK(std::isfinite(profiler.samples()[0].milliseconds));
    REQUIRE_CALL(profiler.beginFrame());
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    auto region = profiler.beginRegion(pass, "Reused slot");
    CHECK(region != GpuProfiler::kInvalidRegion);
    // Incomplete recordings are rejected and can be abandoned safely.
    CHECK(profiler.endFrame() == SLANG_E_INVALID_ARG);
    profiler.endRegion(pass, region);
    pass->end();
    REQUIRE_CALL(profiler.endFrame());
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    REQUIRE_CALL(profiler.poll());
    REQUIRE(profiler.samples().size() == 1);
    CHECK(profiler.samples()[0].name == "Reused slot");
}

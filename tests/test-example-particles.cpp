#include "testing.h"
#include "../examples/particles/renderer.h"
#include "../examples/particles/simulation-clock.h"
#if SLANG_RHI_BUILD_TESTS_WITH_GLFW
#define EXAMPLE_DIR SLANG_RHI_TESTS_DIR
#include "../examples/base/example.h"
#undef EXAMPLE_DIR
#endif

#include <cmath>
#include <cstring>

using namespace rhi;
using namespace rhi::testing;

#if SLANG_RHI_BUILD_TESTS_WITH_GLFW
TEST_CASE("example-particles-input-routing")
{
    REQUIRE(detail::getExamples().empty());
    if (!glfwInit())
        SKIP("GLFW is unavailable");
    struct Cleanup
    {
        ~Cleanup()
        {
            detail::getExamples().clear();
            detail::mainExample = nullptr;
            detail::mouseSourceWindow = nullptr;
            glfwTerminate();
        }
    } cleanup;
    struct Probe : ExampleBase
    {
        Result init(DeviceType) override { return SLANG_OK; }
        void shutdown() override {}
        Result update(double) override { return SLANG_OK; }
        Result draw() override { return SLANG_OK; }
        void onMousePosition(float x, float y) override
        {
            source = getMouseSourceWindow();
            int width, height;
            glfwGetWindowSize(source, &width, &height);
            normalized = {x / width, y / height};
        }
        GLFWwindow* source = nullptr;
        math::float2 normalized;
    } first, second;
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    first.m_window = glfwCreateWindow(640, 480, "Input test primary", nullptr, nullptr);
    second.m_window = glfwCreateWindow(960, 720, "Input test secondary", nullptr, nullptr);
    glfwDefaultWindowHints();
    REQUIRE(first.m_window);
    REQUIRE(second.m_window);
    detail::getExamples() = {&first, &second};
    detail::mainExample = &first;
    // Source identity reaches every receiver before its callback. The secondary
    // window has different dimensions, exposing main-window normalization bugs.
    detail::glfwCursorPosCallback(second.m_window, 480, 360);
    for (auto probe : {&first, &second})
    {
        CHECK(probe->source == second.m_window);
        CHECK(probe->normalized.x == 0.5f);
        CHECK(probe->normalized.y == 0.5f);
    }
    detail::glfwCursorPosCallback(first.m_window, 160, 120);
    for (auto probe : {&first, &second})
    {
        CHECK(probe->source == first.m_window);
        CHECK(probe->normalized.x == 0.25f);
        CHECK(probe->normalized.y == 0.25f);
    }
    first.destroyWindow();
    CHECK(second.getMouseSourceWindow() == second.m_window);
}
#endif

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

TEST_CASE("example-particles-logo-targets")
{
    auto targets = particles::createLogoTargets();
    auto repeated = particles::createLogoTargets();
    REQUIRE(targets.size() == PARTICLE_LOGO_TARGET_COUNT);
    REQUIRE(repeated.size() == targets.size());
    CHECK(std::memcmp(targets.data(), repeated.data(), targets.size() * sizeof(targets[0])) == 0);
    uint32_t materials[2] = {};
    for (const auto& target : targets)
    {
        REQUIRE(std::isfinite(target.x));
        REQUIRE(std::isfinite(target.y));
        CHECK(std::abs(target.x) < 2);
        CHECK(std::abs(target.y) < 2);
        REQUIRE((target.z == 0 || target.z == 1));
        ++materials[uint32_t(target.z)];
    }
    CHECK(materials[0] > 1000);
    CHECK(materials[1] > 1000);
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

    // A fast sweep must affect the middle of its path, not just the endpoints.
    // Compare identical seeded populations; compaction may reorder particles.
    math::float2 brushCenter = {first[0].positionAge.x, first[0].positionAge.y};
    auto brushStep = [&](math::float2 start, math::float2 end, math::float2 velocity)
    {
        reset();
        step(200, 1.0f / 120);
        auto encoder = queue->createCommandEncoder();
        particles::SimulationParams params;
        params.spawnCount = 0;
        params.mousePrevious = start;
        params.mouse = end;
        params.mouseVelocity = velocity;
        REQUIRE_CALL(renderer.step(encoder, params));
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        std::vector<particles::Particle> state(count());
        REQUIRE_CALL(device->readBuffer(renderer.particleBuffer(), 0, state.size() * sizeof(state[0]), state.data()));
        std::sort(
            state.begin(),
            state.end(),
            [](const auto& a, const auto& b)
            {
                return a.targetStyle.z < b.targetStyle.z;
            }
        );
        return state;
    };
    auto untouched = brushStep({}, {}, {});
    auto stationary = brushStep(brushCenter, brushCenter, {});
    auto swept = brushStep(brushCenter - math::float2(1, 0), brushCenter + math::float2(1, 0), {6, 0});
    REQUIRE(untouched.size() == 200);
    REQUIRE(stationary.size() == untouched.size());
    REQUIRE(swept.size() == untouched.size());
    uint32_t disturbed = 0, distant = 0;
    for (size_t i = 0; i < untouched.size(); ++i)
    {
        auto base = untouched[i].velocityLife;
        CHECK(stationary[i].velocityLife.x == base.x);
        CHECK(stationary[i].velocityLife.y == base.y);
        float change = math::length(math::float2(swept[i].velocityLife.x - base.x, swept[i].velocityLife.y - base.y));
        CHECK(std::isfinite(change));
        if (change > 0.1f)
            ++disturbed;
        if (std::abs(untouched[i].positionAge.y - brushCenter.y) > 0.6f ||
            std::abs(untouched[i].positionAge.x - brushCenter.x) > 1.6f)
        {
            CHECK(change < 1e-6f);
            ++distant;
        }
    }
    CHECK(disturbed > 0);
    CHECK(distant > 0);
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

    // Exercise a complete fixed-step choreography, including the automatic
    // burst. Batching avoids a CPU/GPU round trip for every simulation step.
    auto advance = [&](uint32_t steps, uint32_t spawn)
    {
        while (steps)
        {
            uint32_t batch = std::min(steps, 120u);
            auto encoder = queue->createCommandEncoder();
            particles::SimulationParams params;
            params.spawnCount = spawn;
            for (uint32_t i = 0; i < batch; ++i)
                REQUIRE_CALL(renderer.step(encoder, params));
            REQUIRE_CALL(queue->submit(encoder->finish()));
            REQUIRE_CALL(queue->waitOnHost());
            steps -= batch;
        }
    };
    auto readParticles = [&]()
    {
        std::vector<particles::Particle> result(count());
        REQUIRE_CALL(
            device->readBuffer(renderer.particleBuffer(), 0, result.size() * sizeof(result[0]), result.data())
        );
        return result;
    };
    auto meanTargetDistance = [&](const std::vector<particles::Particle>& state)
    {
        double distance = 0;
        for (const auto& p : state)
        {
            float dx = p.positionAge.x - p.targetStyle.x;
            float dy = p.positionAge.y - p.targetStyle.y;
            REQUIRE(std::isfinite(dx));
            REQUIRE(std::isfinite(dy));
            distance += std::sqrt(dx * dx + dy * dy);
        }
        return distance / state.size();
    };
    reset();
    advance(PARTICLE_BURST_TICK, 16);
    CHECK(std::strcmp(renderer.phaseName(), "FORM") == 0);
    auto formed = readParticles();
    REQUIRE(formed.size() > capacity / 2);
    CHECK(meanTargetDistance(formed) < 0.025);
    auto formedImage = render(true, true);
    CHECK(formedImage == render(false, true));
    advance(1, 0);
    CHECK(std::strcmp(renderer.phaseName(), "BURST") == 0);
    auto burst = readParticles();
    double outwardSpeed = 0;
    for (const auto& p : burst)
    {
        float radius = std::sqrt(p.positionAge.x * p.positionAge.x + p.positionAge.y * p.positionAge.y);
        outwardSpeed +=
            (p.positionAge.x * p.velocityLife.x + p.positionAge.y * p.velocityLife.y) / std::max(radius, 0.01f);
    }
    CHECK(outwardSpeed / burst.size() > 2);
    advance(30, 0);
    CHECK(meanTargetDistance(readParticles()) > 0.5);
    CHECK(render(true, true) != formedImage);
    advance(PARTICLE_CYCLE_TICKS - PARTICLE_BURST_TICK - 31 + 1, 16);
    CHECK(std::strcmp(renderer.phaseName(), "FLOW") == 0);
    renderer.burst();
    advance(1, 0);
    CHECK(std::strcmp(renderer.phaseName(), "BURST") == 0);
    reset();
    CHECK(std::strcmp(renderer.phaseName(), "FLOW") == 0);
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

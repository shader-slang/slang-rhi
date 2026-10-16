#include "testing.h"
#include "../examples/base/gpu-profiler.h"

using namespace rhi;
using namespace rhi::testing;

namespace {

// Fail before touching the real queue, so the same device/query pools can be
// used to verify recovery without provoking a driver or debug-layer error.
class FailingQueue : public ICommandQueue
{
public:
    uint32_t submissions = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID&, void** out) override
    {
        *out = nullptr;
        return SLANG_E_NO_INTERFACE;
    }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }
    virtual SLANG_NO_THROW QueueType SLANG_MCALL getType() override { return QueueType::Graphics; }
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(
        const CommandEncoderDesc&,
        ICommandEncoder**
    ) override
    {
        return SLANG_FAIL;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL submit(const SubmitDesc&) override
    {
        ++submissions;
        return SLANG_FAIL;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override { return SLANG_OK; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle*) override { return SLANG_E_NOT_AVAILABLE; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getTimestampCalibration(TimestampCalibration*) override
    {
        return SLANG_E_NOT_AVAILABLE;
    }
};

} // namespace

GPU_TEST_CASE("example-gpu-profiler-lifecycle", ALL)
{
    GpuProfiler profiler;
    REQUIRE_CALL(profiler.init(device));
    if (!profiler.supported())
        SKIP("Timestamp queries not supported");
    auto queue = device->getQueue(QueueType::Graphics);
    FailingQueue failingQueue;

    // More abandoned recordings than ring slots must not exhaust the ring.
    for (uint32_t i = 0; i < GpuProfiler::kFrameCount * 3; ++i)
    {
        auto encoder = queue->createCommandEncoder();
        GpuProfiler::Frame frame(profiler);
        REQUIRE_CALL(frame.begin());
        auto pass = encoder->beginComputePass();
        auto region = profiler.beginRegion(pass, "Discarded");
        REQUIRE(region != GpuProfiler::kInvalidRegion);
        if (i % 3 != 2)
            profiler.endRegion(pass, region);
        pass->end();
        auto commands = encoder->finish();
        REQUIRE(commands);
        if (i % 3 == 1)
            CHECK(frame.submit(&failingQueue, commands) == SLANG_FAIL);
        else if (i % 3 == 2)
            CHECK(frame.submit(&failingQueue, commands) == SLANG_E_INVALID_ARG);
        // The remaining case abandons a finished recording without submitting it.
    }
    CHECK(failingQueue.submissions == GpuProfiler::kFrameCount);
    CHECK(profiler.droppedFrames() == 0);
    CHECK(profiler.samples().empty());

    auto encoder = queue->createCommandEncoder();
    GpuProfiler::Frame frame(profiler);
    REQUIRE_CALL(frame.begin());
    GpuProfiler::Frame nested(profiler);
    CHECK(nested.begin() == SLANG_E_INVALID_ARG);
    for (const char* name : {"Simulation", "Simulation", "Render"})
    {
        auto pass = encoder->beginComputePass();
        auto region = profiler.beginRegion(pass, name);
        REQUIRE(region != GpuProfiler::kInvalidRegion);
        profiler.endRegion(pass, region);
        pass->end();
    }
    auto commands = encoder->finish();
    REQUIRE(commands);
    REQUIRE_CALL(frame.submit(queue, commands));
    CHECK(frame.submit(queue, commands) == SLANG_E_INVALID_ARG);
    REQUIRE_CALL(queue->waitOnHost());
    REQUIRE_CALL(profiler.poll());
    REQUIRE(profiler.samples().size() == 2);
    CHECK(profiler.samples()[0].name == "Simulation");
    CHECK(profiler.samples()[1].name == "Render");
    for (const auto& sample : profiler.samples())
        CHECK(sample.milliseconds >= 0);
    CHECK(profiler.droppedFrames() == 0);
}

GPU_TEST_CASE("example-gpu-profiler-toggle", ALL)
{
    GpuProfiler profiler;
    REQUIRE_CALL(profiler.init(device));
    auto queue = device->getQueue(QueueType::Graphics);
    auto record = [&](uint32_t count, const char* name, bool expectTiming)
    {
        auto encoder = queue->createCommandEncoder();
        GpuProfiler::Frame frame(profiler);
        REQUIRE_CALL(frame.begin());
        auto pass = encoder->beginComputePass();
        for (uint32_t i = 0; i < count; ++i)
        {
            auto region = profiler.beginRegion(pass, name);
            CHECK((region != GpuProfiler::kInvalidRegion) == (expectTiming && i < GpuProfiler::kMaxRegions));
            profiler.endRegion(pass, region);
        }
        pass->end();
        auto commands = encoder->finish();
        REQUIRE(commands);
        REQUIRE_CALL(frame.submit(queue, commands));
    };
    record(GpuProfiler::kMaxRegions + 1, "Old", profiler.supported());
    // Toggle before polling: late results must not reappear after re-enabling.
    profiler.setEnabled(false);
    REQUIRE_CALL(queue->waitOnHost());
    REQUIRE_CALL(profiler.poll());
    CHECK(profiler.samples().empty());
    record(1, "Disabled", false);
    REQUIRE_CALL(queue->waitOnHost());
    profiler.setEnabled(true);
    record(1, "New", profiler.supported());
    REQUIRE_CALL(queue->waitOnHost());
    REQUIRE_CALL(profiler.poll());
    if (profiler.supported())
    {
        REQUIRE(profiler.samples().size() == 1);
        CHECK(profiler.samples()[0].name == "New");
    }
    else
        CHECK(profiler.samples().empty());
}

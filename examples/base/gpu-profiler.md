# GPU timing in examples

`gpu-profiler.h` supplies a device-local `rhi::GpuProfiler` for named GPU regions.
It has no window, particle, HUD, or renderer dependencies. The particle and raster
examples both use it. Other examples can opt in without changing their submission
or presentation flow.

```cpp
GpuProfiler profiler;
SLANG_RETURN_ON_FAIL(profiler.init(device));

// Each frame, before recording regions:
SLANG_RETURN_ON_FAIL(profiler.beginFrame());
auto encoder = queue->createCommandEncoder();
auto pass = encoder->beginComputePass();
auto region = profiler.beginRegion(pass, "Simulation");
// Bind the pipeline and resources, then dispatch work here.
profiler.endRegion(pass, region);
pass->end();
SLANG_RETURN_ON_FAIL(profiler.endFrame());
SLANG_RETURN_ON_FAIL(queue->submit(encoder->finish()));

// Results belong to the latest completed profiled frame, not this submission.
for (const auto& sample : profiler.samples())
    printf("%s: %.3f ms\n", sample.name.c_str(), sample.milliseconds);
```

Regions use `IPassEncoder::writeTimestamp`, so compute, render, and ray-tracing
passes share the same interface. A region may begin and end in different passes
on the same queue. Regions may nest or repeat names; each completed pair produces
one sample. Consumers decide how to aggregate or smooth those samples.

The profiler maintains four independent pools of 32 timestamp pairs. `beginFrame`
polls `IQueryPool::getResultState`; it reads and recycles a pool only after its
submitted results are host-readable. When all pools are busy it skips profiling
that frame, increments `droppedFrames()`, and leaves rendering unaffected. Extra
regions beyond the per-frame limit return an invalid token and are ignored.
There are no queue waits or fence dependencies added to rendering.

Initialize once per device, use one profiler per queue, and submit each profiled
recording exactly once. Finish every started region before `endFrame`; an
unclosed region returns an error. If abandoning a recording, call `cancelFrame`
instead of `endFrame` and discard its command encoder. Keep the profiler alive
through outstanding submissions and wait for the queue before destroying its owner.

Devices without `Feature::TimestampQuery` or a nonzero timestamp frequency have
`supported() == false`; frame and region operations become no-ops. An empty sample
list on a supported device means no measured frame has completed yet. Results use
the device's timestamp frequency to report elapsed milliseconds, without attempting
CPU/GPU clock correlation. `poll()` is also available for headless tests or tools.

GPU elapsed times can include queue scheduling and contention. They exclude work
outside the region markers and must not be presented as application frame time.

# GPU timing in examples

`gpu-profiler.h` supplies `rhi::GpuProfiler` for explicitly marked work on one
queue. Particles, raster, path tracer, and shader toy use it. Surface and triangle
remain introductory examples without profiling instrumentation.

## Shared setup and display

An example calls `initGpuProfiler(device)` once during initialization and uses
`getGpuProfiler()` when recording. `ExampleBase` owns the profiler, adds the F4
shortcut, and appends timings in `drawOverlay()`. Profiling starts enabled; F4
toggles it across backend windows independently of overlay visibility and VSync.
Shutdown must wait for outstanding submissions before destroying the example.

Repeated region names are summed within each completed frame. The shared overlay
averages the observed frame samples and refreshes approximately four times per
second. A changed region list starts a fresh averaging interval, so paused
simulation, disabled bloom, and tracing-mode changes do not retain old timings.
CPU-backend measurements are labeled execution time, rather than GPU time.
Unsupported devices show timing unavailable and continue rendering.

## Recording and submission

```cpp
ComPtr<ICommandEncoder> encoder;
SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(encoder.writeRef()));
GpuProfiler::Frame frame(getGpuProfiler());
SLANG_RETURN_ON_FAIL(frame.begin());

auto pass = encoder->beginComputePass();
auto region = getGpuProfiler().beginRegion(pass, "Simulation");
// Bind the pipeline and resources, then dispatch work here.
getGpuProfiler().endRegion(pass, region);
pass->end();

ComPtr<ICommandBuffer> commands;
SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
SLANG_RETURN_ON_FAIL(frame.submit(queue, commands));
```

The frame guard checks that all recorded regions are closed before submission.
It marks a query pool pending only after the queue accepts the submission.
Early returns, failed encoding, and failed submissions cancel the measurement
when the guard leaves scope. Discard those unsubmitted command buffers; do not
submit them later after their query pool has been reused. The `SubmitDesc`
overload preserves fence waits/signals, including particle readback signaling.
Do not submit a profiled recording outside the guard or submit it twice.

Regions use `IPassEncoder::writeTimestamp`, supporting compute, render, and
ray-tracing passes. A region can span several passes on the same queue. Frame
guards cannot overlap, and all profiler access belongs to the recording thread.
The profiler can also be used independently of `ExampleBase`; `samples()` returns
the latest completed frame's aggregated regions and `sampleSerial()` identifies
that frame. `poll()` does not block.

## Measurement boundaries

- Particles measures simulation, particle/logo rendering, bloom, and tone mapping.
- Raster measures an overall rendering interval plus shadows, scene, bloom, and
  tone mapping. Its overall interval overlaps the individual regions; do not add
  them together to calculate a total.
- Path tracer measures tracing, accumulation, and tone mapping. Ray-pipeline and
  ray-query modes have distinct labels.
- Shader toy measures the selected shader's compute dispatch.

These measurements exclude overlay rendering, presentation, and application-loop
waits. They also exclude work outside the marked intervals, such as output blits
and some render-pass attachment operations. In particular, timestamps inside an
empty render pass do not reliably measure its load-operation clear; surface is
left uninstrumented. GPU intervals can include scheduling and contention, and
are not interchangeable with application frame time or FPS.

## Query lifetime and limits

Four independent query pools each hold up to 32 timestamp pairs. A pool is reused
only after its submitted results are readable. When all pools are busy, the
profiler skips measurement and increments `droppedFrames()`; rendering continues
without an extra wait. Additional regions beyond the per-frame limit return an
invalid token and are ignored. Toggling profiling drains pending queries while
suppressing their old results, and enabling starts with fresh measurements.

Devices without `Feature::TimestampQuery` or a nonzero timestamp frequency do not
allocate pools. Explicitly unsupported query creation also leaves profiling
unavailable. Unexpected initialization, polling, and submission failures remain
visible through `Result`; they are not silently treated as missing support.

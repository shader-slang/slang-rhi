# Slang RHI Stress Test Suite

## Summary
Add the stress suite to the existing `slang-rhi-tests` binary for v1, not a separate app. The current runner already handles backend selection, adapter selection, debug-layer setup, temp directories, shader search paths, runtime skips, cached devices, device availability checks, custom doctest reporting, and final live-object checks. Stress tests should be opt-in through new CLI flags and grouped under `stress-*` test names so normal PR/local test runs stay fast.

A separate `slang-rhi-stress` executable can come later only if we need process isolation, crash recovery between scenarios, external watchdog integration, farm-style unattended orchestration, or per-scenario process memory accounting. If that happens, it should reuse the same `tests/stress/` helpers instead of duplicating scenarios.

## Review Notes
- The plan fits the existing harness well. The best v1 path is to extend `tests/testing.h`, `tests/testing.cpp`, and `tests/main.cpp`, then add explicitly listed stress source files in `CMakeLists.txt`.
- The current test target lists every test source manually, so new files under `tests/stress/` must be added to `target_sources(slang-rhi-tests PRIVATE ...)`.
- `GpuTestFlags` already has room for non-device flags at bit 10 and above. Add `Stress` above the existing `DontCreateDevice`/`DontCacheDevice` flags or move those flags upward if needed.
- The existing `deferred-delete-stress` test is a useful prototype. V1 should either rename/migrate it to `stress-resource-lifetime` or leave it as focused regression coverage and use its shader/work pattern as the seed for the broader stress scenario.
- Cache and timing details can be gathered without public API changes by using `DeviceExtraOptions::persistentPipelineCache`, `DeviceExtraOptions::enableCompilationReports`, and `IDevice::getCompilationReportList`.

## Lessons From Other RHIs
- `wgpu` separates unit, validation/noop, GPU, benchmark, and CTS-style tests; GPU tests use a custom harness across available GPUs, while benchmarks run minimally in tests and fully through benchmarking commands. Lesson: keep stress/benchmark behavior opt-in and harness-driven, not mixed into every normal test run. Source: [wgpu testing guide](https://raw.githubusercontent.com/gfx-rs/wgpu/trunk/docs/testing.md).
- Vulkan CTS uses one main runner with case lists, device selection, base seed, iteration count, fractions, validation toggles, shader-cache controls, logging, and device-loss termination. Lesson: prioritize reproducible run control over many binaries. Source: [Vulkan CTS README](https://raw.githubusercontent.com/KhronosGroup/VK-GL-CTS/main/external/vulkancts/README.md).
- ANGLE exposes dedicated test targets such as end-to-end and white-box tests alongside samples. Lesson: target separation is useful, but after the common harness has proven itself. Source: [ANGLE dev setup](https://raw.githubusercontent.com/google/angle/main/doc/DevSetup.md).

## Key Changes
- Add test-harness support:
  - `GpuTestFlags::Stress`
  - `GPU_STRESS_TEST_CASE(name, flags)` macro that ORs `Stress` into `flags`
  - stress-gated execution in `gpuTestTrampoline`
  - stress-aware skip accounting so unavailable stress tests do not cause "zero tests executed" failures in normal runs
- Add CLI options:
  - `-stress`
  - `-stress-duration-sec=N`, default `300`
  - `-stress-iterations=N`, default `0`; nonzero overrides duration
  - `-stress-seed=N`, default fixed seed `0x5a17c0de`
  - `-stress-inflight=N`, default `8`
  - `-stress-resource-budget-mb=N`, default `256`
  - `-stress-report-interval-sec=N`, default `30`
  - `-stress-log-ops=N`, default `128`; number of recent operations retained for failure output
  - `-stress-validate-every=N`, default `64`; forces readback/canary checks every N iterations
  - `-stress-shader-corpus=N`, default `32`; number of generated shader variants used by shader/cache stress
  - `-stress-enable-rt`, default off unless explicitly requested for soaks; enables ray-tracing stress cases where supported
- Add shared stress helpers under `tests/stress/`:
  - deterministic RNG and weighted operation selection
  - bounded in-flight submit window using repeated submits and delayed host waits
  - operation ring log for reproducible failure diagnostics
  - resource budget accounting for buffers, textures, views, samplers, acceleration structures, scratch buffers, staging/readback buffers, shader programs, and pipelines
  - periodic `waitOnHost` plus canary readback validation
  - stats for iterations, submits, waits, resources created/destroyed, live budget, cache hits/misses/writes, compilation totals, validation checks, failures, and resource-count deltas
  - optional per-scenario temp directory usage through `getCaseTempDirectory()`
- No public Slang RHI API changes.

## Harness Integration Details

### Options
Extend `rhi::testing::Options` in `tests/testing.h`:

```cpp
struct StressOptions
{
    bool enabled = false;
    uint32_t durationSec = 300;
    uint32_t iterations = 0;
    uint64_t seed = 0x5a17c0de;
    uint32_t inflight = 8;
    uint32_t resourceBudgetMB = 256;
    uint32_t reportIntervalSec = 30;
    uint32_t logOps = 128;
    uint32_t validateEvery = 64;
    uint32_t shaderCorpus = 32;
    bool enableRayTracing = false;
};
```

Add `StressOptions stress;` to `Options`.

Parse these in `tests/main.cpp` using the same doctest helpers already used for `-verbose`, `-check-devices`, `-list-devices`, `-select-devices=`, and `-optix-version=`.

### Registration And Skipping
- Add `Stress = (1 << 12)` or another free high bit to `GpuTestFlags`.
- If `Stress` is set and `options().stress.enabled` is false, report a skip reason such as `"stress tests require -stress"` and return before device availability checks.
- Ensure this skip path does not increment normal GPU execution expectations in a way that trips `checkNoSilentGpuSkips()`. The easiest route is to increment `sGpuTestsEncountered` only after the stress gate has passed, or track stress encounters separately.
- Keep test names as `stress-resource-lifetime.<device>`, `stress-shader-cache-execution.<device>`, etc., so doctest `-tc="stress-*"` continues to work.

### Device Creation
- Most stress tests should use `DontCacheDevice` so long runs do not share backend state with unrelated tests.
- Shader/cache stress should use `DontCreateDevice` and create its own device with `DeviceExtraOptions`:
  - `persistentPipelineCache` set to an instrumented in-memory cache
  - `enableCompilationReports = true`
  - optional `enableValidation`/`enableRayTracingValidation` depending on scenario
- Resource-lifetime stress can use the default `createTestingDevice` path unless it needs per-test cache/report options.

### Failure Output
On every stress failure, print:
- device type and adapter index
- scenario name
- seed, iteration, operation index, and command-line options
- last N operations from the ring log
- current resource budget stats
- cache stats and compilation totals when applicable
- live resource-count delta when `gResourceCount` is available

This is the minimum data needed to reproduce with:

```bash
slang-rhi-tests -stress -tc="<scenario>.<device>" -stress-seed=<seed> -stress-iterations=<iteration-or-small-window>
```

## Source Layout
Add explicit files; avoid a large monolithic stress test.

```text
tests/stress/
  stress-context.h
  stress-context.cpp
  stress-rng.h
  stress-op-log.h
  stress-cache.h
  stress-shaders.h
  stress-shaders.cpp
  test-stress-resource-lifetime.cpp
  test-stress-shader-cache-execution.cpp
  test-stress-acceleration-structure-lifetime.cpp
  test-stress-mixed.cpp
```

Add these to the `slang-rhi-tests` `target_sources` block in `CMakeLists.txt`. Keep includes rooted through `tests` and `src`, matching the current target include directories.

## Shared Stress Model

### StressContext
`StressContext` should hold:
- `GpuTestContext* ctx`
- `IDevice* device`
- `ICommandQueue* queue`
- parsed `StressOptions`
- scenario name
- deterministic RNG
- operation log
- resource budget tracker
- counters and timers
- initial `gResourceCount` snapshot when accessible

Core methods:
- `bool shouldContinue()`
- `void beginIteration()`
- `void submit(ComPtr<ICommandBuffer> commandBuffer)`
- `void maybeWaitAndValidate()`
- `void waitAndValidateFinal()`
- `void recordOperation(...)`
- `void reportProgressIfDue()`
- `void failWithContext(...)`

### Determinism
- Use a local, fixed algorithm such as PCG32 or xoshiro, not `std::random_device` or implementation-dependent distributions.
- Derive per-device/per-scenario streams from the base seed:
  - `scenarioSeed = hash64(baseSeed, scenarioName, deviceType, adapterIndex)`
- Log both the base seed and derived seed.
- Operation choices and generated shader variants must depend only on the derived seed and stress options.

### In-Flight Window
The in-flight window should model real deferred lifetime hazards:
- Submit command buffers repeatedly without waiting.
- Keep only intentionally retained resources in the stress pool.
- Release temporary resources immediately after the submit that last references them.
- Force `queue->waitOnHost()` when `submittedSinceWait >= stress.inflight` or validation is due.
- Perform final wait before checking canaries, resource-count deltas, and live-object reports.

### Resource Budget
Use an approximate byte budget so the suite remains stable across developer machines:
- Count buffer sizes exactly.
- Count texture sizes conservatively by format bytes-per-pixel times dimensions, mip count, and layer count.
- Count acceleration-structure sizes from `getAccelerationStructureSizes`.
- Count shader programs/pipelines as fixed synthetic weights for operation balancing, not hard memory accounting.
- If a proposed operation would exceed the budget, choose a release/reuse/validation operation instead.

## Stress Scenarios

### `stress-resource-lifetime`
Goal: expose lifetime, deferred delete, resource tracking, barrier, and descriptor/view retention bugs under real GPU work.

Operations:
- create device-local buffers with `ShaderResource`, `UnorderedAccess`, `CopySource`, and `CopyDestination`
- create small 1D/2D/3D textures with common formats such as `R32Uint`, `RGBA8Unorm`, and `RGBA32Float`
- create texture views with varied mip/layer ranges
- create samplers where supported/meaningful
- write deterministic canaries through compute
- copy buffers and textures
- bind temporary resources in compute and simple render passes
- submit, then release temporary objects immediately
- periodically read back compact canary buffers and verify expected values

Start from the current `deferred-delete-stress` pattern, but replace fixed 100-iteration behavior with the shared stress loop and options.

### `stress-shader-cache-execution`
Goal: exercise shader-program creation, pipeline creation, persistent pipeline cache, compilation reports, and actual execution.

Operations:
- generate a finite corpus of deterministic compute and simple render shaders
- vary constants, specialization-like code paths, entry point names, resource binding shapes, and output canaries
- create/destroy shader programs and pipelines repeatedly
- execute every created pipeline at least once before releasing it
- run phases:
  - cold cache: expect misses and writes
  - hot cache: expect hits where `Feature::PipelineCache` is supported
  - churn cache: evict a bounded number of entries and ensure misses recover
- gather `IPersistentCache` stats and `CompilationReportList` totals

Expected behavior:
- Devices without `Feature::PipelineCache` should still run shader execution stress, but cache hit/miss expectations should be disabled or reported as unsupported.
- Corrupt-cache testing should stay out of v1 stress unless it is explicitly scoped, because existing pipeline-cache tests already mark D3D12 corruption behavior as problematic.

### `stress-acceleration-structure-lifetime`
Goal: catch AS lifetime, scratch-buffer, compaction, update, and ray-tracing binding hazards.

Gate:
- require `Feature::AccelerationStructure`
- require `Feature::RayTracing` for trace validation
- require `-stress-enable-rt` unless we decide ray tracing should be part of default stress runs

Operations:
- build tiny BLAS/TLAS pairs from triangle geometry
- vary scratch buffers and release them after submit
- update or rebuild existing AS objects when supported
- optionally compact BLAS with query/copy path
- bind TLAS and trace a simple ray
- validate hit/miss result in a small readback buffer

Implementation references:
- `tests/test-ray-tracing-common.h`
- `tests/test-ray-tracing.cpp`
- `tests/test-cmd-query.cpp`
- `tests/test-acceleration-structure-creation-with-validation.cpp`

### `stress-mixed`
Goal: combine the above into one bounded random workload.

Weighted operation groups:
- 45% resource create/use/release
- 25% copy/render/compute canary work
- 20% shader/cache work
- 10% AS/ray-tracing work when enabled and supported

Weights should automatically renormalize when a feature is unsupported. The mixed test should keep a stricter default budget than individual scenario so it remains useful for one-hour soaks.

## Reporting And Metrics
Print progress every `-stress-report-interval-sec` seconds and once at the end:
- elapsed seconds
- iterations completed
- submits and waits
- current and peak approximate memory budget
- resource creates/releases by type
- live pooled resources by type
- validation count
- cache query/hit/miss/write counts
- compilation totals from `CompilationReportList`
- failures and last operation index

Keep this output concise in normal mode. Use `-verbose` to include per-interval operation distribution and cache details.

## Test Plan
Add short smoke coverage suitable for CI/manual presubmit:

```bash
slang-rhi-tests -stress -tc="stress-*" -stress-iterations=32 -select-devices=d3d12
```

Add a multi-backend smoke run:

```bash
slang-rhi-tests -stress -tc="stress-*" -stress-iterations=32 -select-devices=d3d12,vulkan,cuda
```

Add recommended soak command:

```bash
slang-rhi-tests -stress -tc="stress-*" -select-devices=d3d12,vulkan,cuda -stress-duration-sec=3600 -stress-inflight=16 -stress-resource-budget-mb=1024
```

Add ray-tracing soak command:

```bash
slang-rhi-tests -stress -stress-enable-rt -tc="stress-acceleration-structure-lifetime.*" -select-devices=d3d12,vulkan,cuda -stress-duration-sec=3600 -stress-inflight=8 -stress-resource-budget-mb=1024
```

Validate:
- deterministic reproduction from printed seed
- stress tests are skipped unless `-stress` is set
- normal test runs do not fail skip accounting because stress tests were gated off
- no debug-layer errors in Debug builds
- no leaked live objects after final wait
- bounded memory use over long runs
- graceful skips for unsupported features/backends
- cache stats make sense on D3D12/Vulkan and degrade cleanly on unsupported backends
- final `queue->waitOnHost()` occurs before resource leak/count checks

## Rollout
1. Add CLI/harness gating and one no-op `stress-smoke` case to prove registration, skip behavior, and command-line parsing.
2. Migrate/expand `deferred-delete-stress` into `stress-resource-lifetime`.
3. Add shader/cache execution stress using an instrumented cache and compilation reports.
4. Add AS/ray-tracing stress behind `-stress-enable-rt`.
5. Add `stress-mixed` once the individual operation groups are stable.
6. Decide whether nightly infrastructure needs a separate wrapper executable.

## Assumptions
- V1 avoids intentional OOM, TDR, device-loss, crash recovery, and corrupt-cache testing; those should be separate destructive tests.
- CPU backend participates only in scenarios that make sense without GPU overlap, descriptor lifetime hazards, pipeline cache, or ray tracing.
- Long runs are manual or nightly jobs, not part of default `slang-rhi-tests`.
- The default resource budget should be conservative enough for developer machines and CI agents.
- If process isolation becomes necessary, build a thin `slang-rhi-stress` wrapper around the same stress helpers instead of duplicating scenarios.

## Resolved V1 Decisions
- Ray-tracing/AS stress is opt-in with `-stress-enable-rt`.
- Initial backend focus is D3D12, Vulkan, and CUDA where the relevant features are available.
- Shader stress uses deterministic generated shaders for v1.
- Reporting is plain doctest text output for v1; JSON/CSV artifacts can be added once nightly consumers exist.
- The recommended manual soak remains one hour, while smoke validation should use short iteration-count runs.

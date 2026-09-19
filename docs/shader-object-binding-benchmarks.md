# Shader-object binding benchmark: whole branch versus original base

Measured on 2026-09-19. Reused finalized objects improve several common cases, but this branch is **not a universal performance improvement**. There are measurable mutable-path regressions, and finalizing objects that are used only once is substantially more expensive.

The earlier 44.8x D3D12 / 15.0x Vulkan result measured only the persistent allocator step against an implementation that already cached finalized bindings. This study compares **all changes on the branch** against the original base. It includes setters, native command generation, submission, and object creation in the cases that create objects per call.

## Revisions and method

- Before: `82c03494`, immediately before the first finalized-shader-object commit.
- After: `352b2f49`, persistent buffer pooling, with no subsequent library implementation changes.
- Identical public-API benchmark C++ and Slang sources were compiled into both executables. The baseline source was exported into `build/binding-matrix-base-src`; only benchmark fixtures and their CMake registration were added.
- MSVC 19.39.33523.0, Release `/O2 /Ob2 /DNDEBUG`, Windows 10, Ryzen 9 3900X, NVIDIA TITAN RTX, driver 610.74, Slang 2026.12.2.
- Effective C/C++ flags and backend feature settings matched. Both builds used the same dependency directories. Slang, DXC, Dawn, WinPix, and Agility D3D12 runtime DLL hashes matched.
- Release validation was disabled in both builds. Every warmup and measured sample checked the result after completion. Invalid results stop the comparison runner.
- Three runs per executable, alternating order: before/after, after/before, before/after. Each run discards one warmup per mode, then measures seven samples with rotating mode order. Tables report the median of the three run medians. Raw per-sample timings and run-median ranges are retained.
- The main matrix uses 1,024 calls per command buffer and inherited CPU affinity. A second matrix uses 64 calls. A focused repetition pins both executables to the same logical processor (mask `0x4`). All use above-normal process priority. Pinning also constrains backend/driver worker threads, so absolute times from pinned and unpinned series should not be mixed.

The base's `finalize()` does not enable persistent binding preparation. "Frozen" rows on the base issue the same finalization calls, respect immutability, and bind the same objects and values; they do not receive the new optimization.

## Workloads

Every graph has two parameter blocks. Each block contains four structured-buffer bindings and roughly 256 bytes of uniform data. The root has a changing scalar and a 256-byte array. Material identity changes both uniform values and the bound input resource. The shader consumes both blocks, the root scalar, and the beginning/end of the arrays.

| Mode | Work inside the measured loop |
| --- | --- |
| `static-mutable` | Rebind and dispatch/draw the same unchanged mutable root |
| `static-frozen-root` | Same graph, with the root recursively finalized |
| `root-mutable-blocks` | Change a root scalar every call; both blocks stay mutable |
| `root-one-frozen-block` | Change the root; reuse one finalized block and one mutable block |
| `root-two-frozen-blocks` | Change the root; reuse two finalized blocks |
| `mutate-blocks` | Change the root and update both blocks' uniforms and four resource bindings every call |
| `swap-mutable-blocks` | Change the root and select two mutable blocks from a set of 64 |
| `swap-frozen-blocks` | Same selection pattern with 64 finalized blocks |
| `fresh-mutable-blocks` | Create, populate, and attach two new mutable blocks for every call |
| `fresh-frozen-blocks` | Same, including finalization and first-use preparation for every call |
| `rotate-mutable-roots` | Cycle through 64 prepopulated mutable roots with different arguments and blocks |
| `rotate-frozen-roots` | Same root set, finalized and reused across command buffers |
| `bulk-root-two-frozen-blocks` | Change the root scalar and all 256 array bytes; reuse two finalized blocks |

Compute executes one thread per dispatch and accumulates into a UAV. Graphics draws a full-screen triangle into a one-pixel RGBA32Float target with additive blending, verifying both the accumulated value and draw count. Ray tracing launches one ray per call against a real triangle BLAS/TLAS, alternates hits and misses when arguments change, and checks payload contributions. Acceleration-structure and pipeline creation are outside timing.

These are deliberately small GPU workloads that expose binding overhead. They cover buffer-heavy binding graphs, not a full renderer: they do not establish performance for texture/sampler-heavy materials, GPU-heavy kernels, pipeline switching, multi-threaded recording, or other GPUs. Metal could not be compiled or measured on this Windows machine.

## Timing definitions

All values are nanoseconds per dispatch/draw/trace call, measured over an entire command buffer:

- `setup`: encoder creation, output reset, initial timestamp, pass setup, and graphics viewport/scissor setup.
- `encode`: setters, block swaps/creation/finalization as applicable, pipeline binding, and dispatch/draw/trace calls.
- `finish`: pass end, final timestamp, and `finish()`; this includes native command generation on backends that defer it.
- `submit`: queue submission. Some backends perform substantial work or wait inside this call.
- `retire`: releasing the local command-buffer and encoder references after the queue wait. Any allocator recycling deferred to the next encoder is included in its setup.
- `cpu`: setup + encode + finish + submit + retire, measured per sample before computing medians. This is elapsed host API time, not CPU cycle accounting.
- `wall`: encoder creation through queue completion and local-reference release, including the wait.
- `gpu`: timestamp interval around the pass. It can include gaps while the CPU feeds the queue, especially CUDA. It is not isolated shader execution time. CPU timestamp queries are CPU intervals; WebGPU did not expose timestamps in this configuration.

Readback and validation are outside all reported timings. Reusable objects are constructed before the samples and survive across command buffers; those rows measure warm reuse. The fresh-block rows include creation, setters, finalization, and first preparation in `encode`. Component medians need not sum to the median total. GPU and host times overlap and must not be added.

## Main results: total host API speedup

Values greater than 1 are faster on the branch; values below 1 are slower. All rows below are from the same unpinned 1,024-call matrix, including the noisy CUDA observations.

| Workload | Backend | Changing root, mutable blocks | One frozen block | Two frozen blocks | Swap frozen blocks | Same frozen root |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Compute | CPU | 0.93x | 1.13x | 1.28x | 1.04x | 2.96x |
| Compute | CUDA | 0.81x | 0.84x | 0.87x | 0.87x | 0.94x |
| Compute | D3D11 | 0.93x | 1.04x | 1.09x | 0.97x | 4.96x |
| Compute | D3D12 | 1.01x | 1.14x | 1.34x | 1.25x | 2.77x |
| Compute | Vulkan | 0.99x | 1.11x | 1.36x | 1.25x | 2.46x |
| Compute | WebGPU | 0.98x | 1.16x | 1.54x | 1.49x | 3.64x |
| Graphics | D3D11 | 0.96x | 0.97x | 1.08x | 0.96x | 2.88x |
| Graphics | D3D12 | 0.97x | 1.18x | 1.49x | 1.32x | 6.32x |
| Graphics | Vulkan | 0.98x | 1.19x | 1.66x | 1.39x | 5.04x |
| Graphics | WebGPU | 1.07x | 1.09x | 1.54x | 1.46x | 6.40x |
| Ray tracing | D3D12 | 1.05x | 1.07x | 1.07x | 1.12x | 1.59x |
| Ray tracing | Vulkan | 0.94x | 1.04x | 1.24x | 1.23x | 1.66x |

For the typical two-finalized-block case, compute host API time was 2.970 -> 2.222 us on D3D12, 2.538 -> 1.862 us on Vulkan, and 9.446 -> 6.129 us on WebGPU. Graphics measured 2.163 -> 1.454 us, 1.906 -> 1.148 us, and 6.889 -> 4.465 us, respectively.

Ray-tracing improvements in total host API time are smaller than encoding improvements because native command/SBT processing still contributes significantly. A reused finalized root is a stronger optimization than finalized blocks in a changing root, but it fits fewer workloads.

Smaller command buffers change the result: at 64 calls, the two-block compute case was 1.22x on D3D12, 1.62x on Vulkan, and 1.08x on WebGPU. For graphics those figures were 1.37x, 1.46x, and 1.06x. Thus the large-batch WebGPU gains should not be assumed for small submissions.

## Regressions and unfavorable cases

### Objects used only once

Creating and finalizing two new blocks for every compute dispatch is slower on every backend:

| Backend | Base total host API time (us/call) | Branch (us/call) | Slowdown |
| --- | ---: | ---: | ---: |
| CPU | 6.066 | 12.617 | 2.08x |
| CUDA | 10.709 | 91.614 | 8.55x |
| D3D11 | 6.614 | 23.444 | 3.54x |
| D3D12 | 7.595 | 12.175 | 1.60x |
| Vulkan | 9.619 | 16.040 | 1.67x |
| WebGPU | 15.959 | 30.746 | 1.93x |

Graphics and ray tracing show the same direction. Pinning reproduced the cost, including roughly 7.6x on CUDA and 3.0x on D3D11. Pooling reduced the cost of first preparation relative to the previous branch stage; it does not make finalization free or faster than the original transient path for objects used once. Reuse is necessary to recover preparation and retained-record costs. This study does not measure an exact break-even reuse count.

### Mutable controls

Some regressions persist when both executables use the same CPU core. Examples from the pinned 1,024-call series:

| Workload | Base host API time (ns/call) | Branch | Increase |
| --- | ---: | ---: | ---: |
| CPU, rotate mutable roots | 578.8 | 632.8 | 9.3% |
| CPU, fresh mutable blocks | 5961.9 | 6408.9 | 7.5% |
| D3D11 compute, changing root / mutable blocks | 2448.7 | 2522.8 | 3.0% |
| D3D11 graphics, changing root / mutable blocks | 2432.6 | 2485.4 | 2.2% |
| WebGPU compute, rotate mutable roots | 8262.4 | 9723.5 | 17.7% |
| WebGPU graphics, rotate mutable roots | 6889.7 | 8482.8 | 23.1% |

The WebGPU root-rotation regression also appeared in the unpinned runs. Encoding alone understated it: pinned compute encoding increased about 7%, while total host API time increased about 18%. This is a reason to retain finish/submit measurements rather than judging the branch solely on binding-builder time.

Other cases show smaller increases or overlapping run ranges. The CSV includes all controls and run-median ranges, rather than filtering to wins. These measurements locate workloads for follow-up profiling; they do not identify the responsible functions.

### CUDA timing sensitivity and ray-tracing failure

The unpinned mixed CUDA compute runs had sharply different timing regimes: the changing-root mutable control was about 5.54 us on the base, while the branch varied from 5.44 to 6.90 us. Pinning reduced the host-time variation, but mixed-mode GPU intervals and completion latency were still worse on the branch.

Isolating each workload in its own process, without setting up or running the other modes, gave:

| Isolated CUDA compute case | Base / branch host API time (us) | Host speedup | Base / branch completion latency (us) |
| --- | ---: | ---: | ---: |
| Changing root, mutable blocks; five runs each | 5.456 / 5.515 | 0.99x | 7.883 / 8.137 |
| Changing root, two finalized blocks; three runs each | 5.552 / 5.157 | 1.08x | 7.884 / 7.323 |

Mutable encoding was still about 8% slower in isolation (700.5 -> 755.6 ns), but submission dominated the total. Its GPU timestamp run ranges overlapped. The isolated two-block GPU interval was 6.768 -> 6.639 us, also with overlapping ranges. The larger mixed-mode GPU slowdown did not persist at the same magnitude in isolation. Its cause remains unresolved; this study does not attribute it to a change in shader execution speed.

CUDA ray tracing with 1,024 launches failed with invalid CUDA events on **both revisions**, including an isolated ray-tracing test process. Those measurements were excluded rather than treated as performance data. At 64 launches all modes passed on both revisions: two reused finalized blocks improved host API time by 1.05x, while GPU intervals were essentially unchanged. D3D12 and Vulkan ray tracing passed at both sizes.

## Conclusions and next work

The branch is useful for bindings reused across many calls, particularly D3D12/Vulkan/WebGPU graphics and compute. Typical gains are much smaller than the allocator-only microbenchmark suggested. D3D11 block reuse gives small gains, and CUDA launch/submission work limits its total benefit.

Before calling performance validation complete, profile the mutable controls, especially WebGPU root rotation and CPU object creation/traversal. Investigate CUDA's mixed-mode timing sensitivity and the ray-tracing stress failure separately. Application-level validation should include texture/sampler-heavy materials, pipeline changes, substantial GPU work, and Metal hardware. No library fixes were mixed into this measurement study.

## Reproduction and data

- Fixture: [test-benchmark-bindings.cpp](../tests/test-benchmark-bindings.cpp), with the four adjacent `test-benchmark-bindings-*.slang` files. It uses only public RHI interfaces and the existing test helpers.
- Runner: [benchmark-bindings.py](../tools/benchmark-bindings.py).
- All 405 aggregate comparisons, including every timing component and run-median ranges: [bindings-2026-09-19.csv](benchmarks/bindings-2026-09-19.csv).
- Build/environment details, executable hashes, and run settings: [bindings-2026-09-19.json](benchmarks/bindings-2026-09-19.json).
- Raw logs and per-sample CSVs remain under `build/binding-matrix-main`, `build/binding-matrix-small`, `build/binding-matrix-pinned`, `build/binding-matrix-cuda-isolated`, and `build/binding-matrix-cuda-two-frozen`.

The existing baseline build is `build/binding-matrix-base-build/Release/slang-rhi-tests.exe`. To recreate it elsewhere, export revision `82c03494`, copy the five fixture files into `tests`, register the C++ file with the test target, and configure with the current build's compiler flags, feature options, Slang version, and `FETCHCONTENT_SOURCE_DIR_*` paths. Do not benchmark against the allocator-only saved executable.

```powershell
cmake --build build --config Release --target slang-rhi-tests
cmake --build build/binding-matrix-base-build --config Release --target slang-rhi-tests

$before = 'build/binding-matrix-base-build/Release/slang-rhi-tests.exe'
$after = 'build/Release/slang-rhi-tests.exe'

# Main matrix; the known CUDA ray-tracing stress failure is exercised separately.
python tools/benchmark-bindings.py --before $before --after $after `
  --output build/binding-matrix-main --count 1024 `
  --filter 'benchmark-bindings-compute.*,benchmark-bindings-graphics.*,benchmark-bindings-ray-tracing.d3d12,benchmark-bindings-ray-tracing.vulkan'

# Smaller command buffers, including CUDA ray tracing.
python tools/benchmark-bindings.py --before $before --after $after `
  --output build/binding-matrix-small --count 64

# Optional mode isolation: mode 2 is changing root/mutable blocks; mode 4 uses two frozen blocks.
python tools/benchmark-bindings.py --before $before --after $after `
  --output build/binding-matrix-cuda-isolated --filter 'benchmark-bindings-compute.cuda' `
  --affinity-mask 0x4 --mode 2 --runs 5
```

For a direct single-executable run, use `-tc=benchmark-bindings-*` and set `SLANG_RHI_BINDING_BENCHMARK_COUNT`, `SLANG_RHI_BINDING_BENCHMARK_SAMPLES`, and optionally `SLANG_RHI_BINDING_BENCHMARK_MODE` (zero-based index in `kModes`). Default settings are 1,024 calls, seven measured samples, and all modes. The default includes the CUDA ray-tracing stress case; a failure is reported and is not silently skipped. Timings are observational, not test pass/fail thresholds.

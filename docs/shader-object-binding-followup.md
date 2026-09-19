# Focused shader-object binding follow-up

This study compares the pre-branch implementation (`82c03494`) with the implementation at `16948268`. It establishes a baseline for the next optimization stage; it makes no library changes.

## Reuse gains survive isolation

The main sweep contains 84 isolated comparisons: seven modes on six compute backends, four graphics backends, and D3D12/Vulkan ray tracing. The modes are changing root arguments with zero/one/two finalized blocks, swapping mutable/finalized blocks, and rotating mutable/finalized roots. All output checks passed.

For changing root arguments with two reused finalized parameter blocks, total host-time speedups are:

| Backend | Compute | Graphics | Ray tracing |
| --- | ---: | ---: | ---: |
| CPU | 1.26x | — | — |
| CUDA | 1.09x | — | — |
| D3D11 | 1.08x | 1.07x | — |
| D3D12 | 1.34x | 1.50x | 1.17x |
| Vulkan | 1.43x | 1.68x | 1.23x |
| WebGPU | 1.33x | 1.51x | — |

D3D12 saves approximately 0.74 us of encoding per compute dispatch or graphics draw. The percentage is larger in graphics because its other host costs are smaller. Ray tracing retains substantial command-finalization work. CUDA remains dominated by submission. These are binding-heavy fixtures with tiny GPU workloads, not predictions of application frame-rate changes.

Smaller mutable costs remain visible. CPU rotating mutable roots increased from 524.6 to 560.7 ns/call; Vulkan compute from 2406.5 to 2465.6 ns, and Vulkan graphics from 1771.8 to 1828.2 ns. Some D3D11 results have wide, overlapping run ranges; their point estimates alone are insufficient to select an optimization target.

## The large WebGPU regression depends on the surrounding modes

The earlier mixed-mode rotating-root result reproduced. However, running only that mode, or running all reusable modes together, removed the approximately 1 us/call submission penalty. A new mode mask selects both setup and execution, so excluded modes neither allocate fixture objects nor run batches.

The following contemporaneous controls use the same rebuilt normal executables, five alternating pairs each. All times are us per dispatch/draw; the measured mode is always `rotate-mutable-roots`.

| Surrounding workload | Compute host, base → branch | Compute submit | Graphics host, base → branch | Graphics submit |
| --- | ---: | ---: | ---: | ---: |
| Isolated mode | 8.190 → 8.369 | 1.607 → 1.617 | 6.359 → 6.494 | 0.765 → 0.747 |
| All reusable modes, no fresh objects | 7.606 → 7.818 | 1.583 → 1.614 | 6.331 → 6.502 | 0.732 → 0.743 |
| All modes, including fresh objects | 7.668 → 9.969 | 1.579 → 2.606 | 6.380 → 7.921 | 0.731 → 1.765 |

The diagnostic builds localize that extra submission time inside `wgpuQueueSubmit`: compute was 1.532 → 2.640 us/call in the full mixed run, but 1.597 → 1.618 us with reusable modes only. Graphics was 0.740 → 1.775 us in the full run and 0.729 → 0.723 us with reusable modes only. There is still exactly one native submission per batch. Mutable rotation also creates the same number of bind groups and buffers and copies the same byte count on both revisions.

Excluding each fresh-object mode independently narrows the submission effect further (three pairs each):

| Excluded mode; all other modes run | Compute host, base → branch | Compute submit | Graphics host, base → branch | Graphics submit |
| --- | ---: | ---: | ---: | ---: |
| Fresh finalized blocks only | 8.093 → 8.576 | 1.616 → 1.632 | 6.451 → 7.926 | 0.751 → 0.802 |
| Fresh mutable blocks only | 7.885 → 9.991 | 1.601 → 2.640 | 6.347 → 7.828 | 0.737 → 1.733 |

Including one-use finalized blocks is the trigger for the large **submission** penalty in these controlled runs. This does not explain every encoding regression: the graphics control without fresh finalized blocks still increased from 4.603 to 6.002 us/call in encoding. Applications that continually create mutable objects deserve their own control; excluding every fresh-object mode is not a universal solution. In contrast, the mixed reusable-only workload retains only a roughly 3% total difference in rotating mutable roots.

Thus the large mixed-run submission penalty is an execution-history interaction at the native API boundary, not evidence that mutable rotation intrinsically needs more descriptors or submissions. Its precise mechanism inside Dawn/the driver is not identified by these hooks. Encoding differences remain: the latest isolated controls were about 2% slower overall, and an earlier isolated graphics series had a larger encoding regression. The raw run ranges and all repeats are retained rather than selecting only the favorable run.

One-use cases are not included in an aggregate score—there is no aggregate score. Their effect here is on the execution history of other cases sharing the process. The reusable-only mixed control is a useful ongoing regression test alongside isolated cases.

## What the WebGPU profiles show

For 1,024 compute dispatches after warmup:

| Operation per batch | Base, mutable root | Branch, mutable root | Branch, changing root + two finalized blocks | Branch, rotating finalized roots |
| --- | ---: | ---: | ---: | ---: |
| Native bind groups created | 3,072 | 3,072 | 1,024 | 0 |
| Native bind-group bind calls | 3,072 | 3,072 | 3,072 | 3,072 |
| Uniform allocations | 3,072 | 3,072 | 1,024 | 0 |
| Buffers created for uniform upload | 2 | 2 | 2 | 0 |
| Requested uniform-buffer storage | 8 MiB | 8 MiB | 8 MiB | 0 |
| Uniform bytes copied | 1 MiB | 1 MiB | 512 KiB | 0 |

Compute also copies four bytes to clear its output; that is included in the raw copy counters but excluded from the uniform-byte row. Graphics has the same bind-group counts, with one fewer descriptor entry in its root group.

Three opportunities are now concrete:

1. **Transient uniform-page reuse is the largest fixed-cost opportunity on WebGPU.** Each new command encoder creates a new command buffer and its own uniform pool. The first mutable uniform allocation creates a 4 MiB device buffer plus a 4 MiB staging buffer and synchronously waits for mapping. In the two-finalized-block compute profile, buffer creation plus the mapping wait cost about 1.28 ms per 1,024-call batch. At 64 calls they still cost about 1.08 ms, even though only 32 KiB of uniform data is copied. The native submit itself cost another approximately 0.59 ms in that small-batch profile. Normal, uninstrumented small-batch compute was essentially tied overall; graphics improved 1.08x. Reducing descriptor work cannot remove this fixed allocation/mapping cost.
2. **Prepared blocks still get rebound with their mutable parent.** Both revisions issue 3,072 native bind-group calls in the typical two-block case, despite only the root group changing. Those calls take roughly 0.47 us per compute dispatch and 0.51 us per graphics draw in the instrumented branch. Tracking native group identity and pipeline compatibility could avoid the two unchanged calls. That is a measured opportunity, not a claim that two-thirds of the whole binding cost disappears.
3. **Prepared ownership is tracked twice for composed WebGPU groups.** The command buffer retains the prepared record, and composition additionally performs 2,048 native `BindGroupAddRef` calls, with matching releases on retirement. Keeping a clear distinction between owned transient groups and borrowed groups could remove that duplication. The very short reference-count timings are sensitive to timer overhead; call counts establish the redundant work, not a reliable predicted speedup.

The page-pool cost predates this branch. Optimizing it should benefit mutable workloads and the typical finalized-block workload, while reducing the apparent advantage of fully finalized roots. A proper implementation needs pages that remain owned by recorded/in-flight commands and become reusable only when safe, with remapping completed before reuse. Merely changing the page size or adding a benchmark-specific fast path would not address that lifecycle.

## Implications for the next implementation stage

Separating finalized-only state is still a reasonable small cleanup. In matching diagnostic builds, `ShaderObject` grew from 1,600 to 1,720 bytes and `RootShaderObject` from 1,632 to 1,752 bytes. Moving cold prepared-state storage out of mutable objects could reduce that footprint. It is not a demonstrated fix for the large WebGPU mixed-run penalty: mutable objects do not acquire the prepared-data mutex, and object construction is outside the rotating-root timing. Resource-traversal timing in isolated WebGPU rotation increased only from approximately 159 to 173 ns/call in this profile.

Keep that cleanup independently measurable, with mutable controls and finalized reuse checks. For substantial WebGPU improvements, prioritize uniform-page lifetime/reuse and then per-group binding change tracking. Preserve pipeline-compatibility invalidation and resource lifetime/state handling when suppressing redundant native binds. Treat borrowed-group reference ownership as a separate small follow-up. No implementation optimization is included in this study.

## Data and validation

The follow-up contains **230 normal comparison rows from 14,196 checked measured batches**, plus 2,520 checked diagnostic batches covering 60 case/mode comparisons. Warmup batches also validate their output. Native bind-group creation/binding, buffer-creation, and submission counts in the structural table were checked against every raw sample of the eight corresponding isolated diagnostic cases, not just their medians. Dawn and Slang runtime hashes match across all four normal/diagnostic builds.

- [Normal comparisons](benchmarks/bindings-focused.csv), including the initial repeats, small batches, unpinned controls, mode masks, and per-phase timing.
- [Native-call and phase profiles](benchmarks/bindings-wgpu-profile.csv).
- [Build/runtime hashes, settings, and raw-log locations](benchmarks/bindings-focused.json).

Metal was not measured on this Windows machine. The previously documented CUDA ray-tracing stress failure was not revisited; the ray-tracing controls here use D3D12 and Vulkan. The diagnostic hooks locate the native API boundary but do not provide internal Dawn/driver call stacks or explain every allocation-history effect.

## Measurement approach

The main focused suite runs every workload and mode in its own process. Each process creates only that mode's reusable objects, performs one warmup batch, then records seven batches of 1,024 calls. Five alternating before/after process pairs provide run medians and their ranges. Supplemental small-batch, unpinned, mutate/bulk, and exclusion controls use three pairs; the full/reusable-only/isolated mode-mask controls use five. Diagnostics use three pairs. Every batch checks its GPU output. Fresh-object modes are excluded from the main comparison and included explicitly in execution-history controls.

The initial normal Release executables and dependency binaries are the same ones used in the [earlier study](shader-object-binding-benchmarks.md). For the mode-mask controls, both normal test executables were rebuilt with the same fixture change, outside the measured loop. Original executables are preserved as `slang-rhi-tests-binding-pre-mask.exe` in their respective Release directories. The processes run at above-normal priority with affinity `0x4` on the Ryzen 9 3900X / TITAN RTX machine, using the existing AMD Ryzen Balanced power plan. Affinity applies to worker threads too; these are controlled comparisons on this machine, not general backend rankings. A fresh process isolates fixture setup and process-local execution history; it does not reset driver or system state. Supplemental unpinned WebGPU controls also preserve the reuse gains, with mutable rotation approximately 2–3% slower overall.

“Host time” is elapsed time inside setup, encode, finish, submit, and retirement API calls. It includes any blocking inside those calls. The explicit queue wait is excluded from host time and included in completion latency. The reported host median is calculated from complete samples, not by summing independently calculated phase medians. Ratios greater than one mean the branch is faster. Run ranges describe observed variability, not confidence intervals.

Separate diagnostic source exports use identical hooks on both revisions. The hooks count and time resource traversal, uniform allocation, native bind-group creation/binding, and native submission, with separate recording phases. They also count requested buffer bytes and copied bytes. These builds retain `/O2 /Ob2 /DNDEBUG`, but enable only CPU and WebGPU, disable GLFW, and add instrumentation overhead. Their timings locate costs; normal executables supply the performance comparisons. Nested diagnostic timers are inclusive and must not be added together.

## Reproduction

The [focused runner](../tools/benchmark-bindings-focused.py) invokes the existing runner separately for each exact test name and mode, retains all raw logs and samples, and combines the comparison rows. Its default modes cover changing roots with zero/one/two finalized blocks, mutating blocks, swapping mutable/finalized blocks, rotating mutable/finalized roots, and bulk root updates. One-use finalized objects are not part of that default.

```powershell
python tools/benchmark-bindings-focused.py `
  --before build/binding-matrix-base-build/Release/slang-rhi-tests.exe `
  --after build/Release/slang-rhi-tests.exe `
  --output build/binding-focused-repeat --affinity-mask 0x4 `
  --cases benchmark-bindings-compute.wgpu benchmark-bindings-graphics.wgpu

# Mixed reusable modes, excluding both fresh-object modes (8 and 9).
python tools/benchmark-bindings.py `
  --before build/binding-matrix-base-build/Release/slang-rhi-tests.exe `
  --after build/Release/slang-rhi-tests.exe `
  --output build/binding-reused-mixed-repeat --affinity-mask 0x4 --runs 5 `
  --filter benchmark-bindings-compute.wgpu --mode-mask 0x1cff
```

`--mode-mask 0x1dff` excludes only fresh finalized blocks; `0x1eff` excludes only fresh mutable blocks. The fixture reads `SLANG_RHI_BINDING_BENCHMARK_MODE_MASK` as a decimal integer; the Python runner accepts hexadecimal CLI values and converts them. The runner checks that the executable actually honors its requested mode selection, so an older fixture cannot silently run all modes when a mask was requested.

For diagnostics, export each revision to a disposable source directory, copy the public benchmark fixture from the branch into the old export, and register its C++ file with CMake as in the earlier study. Run [profile-bindings-wgpu.py](../tools/profile-bindings-wgpu.py) on each export **before building it**. This installs the [diagnostic header](../tools/benchmark-bindings-wgpu-profile.h) and the same phase/native-call hooks into both exports; it refuses to instrument a checkout. Use matching dependency paths and compiler options for the two builds. The ordinary working tree and normal benchmark executables are unaffected.

```powershell
python tools/profile-bindings-wgpu.py build/binding-profile-before-src
python tools/profile-bindings-wgpu.py build/binding-profile-after-src
# Configure and build the two diagnostic exports.
python tools/benchmark-bindings.py `
  --before build/binding-profile-before-build/Release/slang-rhi-tests.exe `
  --after build/binding-profile-after-build/Release/slang-rhi-tests.exe `
  --output build/binding-profile-repeat --affinity-mask 0x4 `
  --filter benchmark-bindings-compute.wgpu --mode 10 --profile-wgpu
```

`profile-samples.csv` preserves raw per-batch call counts, inclusive nanoseconds, and amounts. `profile-comparison.csv` reports median-of-run-medians for each metric; times are normalized per dispatch/draw, while counts and amounts remain per batch. Amounts mean descriptor entries for `DeviceCreateBindGroup`, requested bytes for `DeviceCreateBuffer`, and copied bytes for `CommandEncoderCopyBufferToBuffer`. Warmup and output verification are excluded. The hooks are intended for this single-threaded fixture, not general application tracing.

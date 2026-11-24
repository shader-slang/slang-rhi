# BLAS creation performance in slang-rhi

2026-11-23

## Summary

With geometry in one big buffer and scratch bump-allocated from one buffer, the remaining CPU
cost of creating many small BLASes is allocating and freeing the BLAS storage itself.

- **D3D12:** 1000 BLASes take about 590 ms with compaction and 190 ms without. The difference is
  almost entirely `createAccelerationStructure` and release for the compacted BLASes. D3D12MA
  makes every buffer of 32 KB or less a committed resource, at about 0.27 ms to create and
  0.1 ms to release. BLASes above 32 KB take the placed path and cost about 0.01 ms.
- **Vulkan:** about 850 ms with compaction, 510 ms without. Every BLAS buffer has its own
  `vkAllocateMemory` (about 0.1 ms) and its own free (about 0.2 ms), regardless of size.
- **CUDA/OptiX:** about 215 ms. Allocation is negligible; the time is `optixAccelBuild` and the
  wait for it.

The numbers above are the starting point. With the D3D12 allocator fix ("D3D12 change") and
VMA on Vulkan ("All three backends with VMA"), 1000 BLASes with compaction take about 233 ms on
D3D12, 209 ms on Vulkan and 223 ms on CUDA/OptiX, and almost all of that is the GPU build.

## Benchmark setup

`blas-benchmark` (`examples/blas-benchmark/blas-benchmark.cpp`) creates N small triangle-mesh
BLASes and times each phase on the CPU. The defaults match the renderer setup:

- All meshes in one vertex buffer and one index buffer (`--geometry shared`).
- One scratch buffer, bump-allocated with 256-byte alignment (`--scratch shared`).

Per run:

1. Query sizes (`getAccelerationStructureSizes`).
2. Create the uncompacted BLASes (`createAccelerationStructure`).
3. Record and submit the builds, each with a compacted-size query.
4. Wait for the GPU.
5. Read compacted sizes, create the compacted BLASes, record and submit the compacting copies,
   wait.
6. Release the BLASes and flush deferred deletes.

`--strategy batched` records everything into one command buffer; `per-blas` submits once per
BLAS. `--no-compact` stops after step 4.

| Item | Value |
| --- | --- |
| GPU | NVIDIA TITAN RTX, 24 GB, driver 617.42 |
| CPU | AMD Ryzen 9 3900X, 12 cores |
| OS | Windows 10 Pro 19045 |
| Build | MSVC 2022, RelWithDebInfo, validation off |
| Mesh | Bumpy grid, 512 triangles, 289 vertices (also 64 and 4096 triangles) |
| Build flags | PreferFastTrace, AllowCompaction |
| Sizes at 512 triangles | 33 KB uncompacted, 14 KB compacted, 6 KB scratch per BLAS |
| Statistic | Median of 4 iterations after one warm-up iteration, same process |

## Results

CPU wall time in ms for 1000 BLASes of 512 triangles, batched into one command buffer, with
compaction. Values are rounded; run-to-run variation is about 10%, and up to 30% for the D3D12
compacted-BLAS creation.

| Phase | D3D12 | Vulkan | CUDA/OptiX |
| --- | --- | --- | --- |
| Create uncompacted BLASes | 9 | 95 | 4 |
| Create compacted BLASes | 275 | 95 | 3 |
| Sizes, record, submit | 20 | 16 | 113 |
| Wait for GPU | 180 | 215 | 83 |
| Release BLASes | 105 | 400 | 10 |
| **Total** | **590** | **850** | **213** |
| Allocation and release share | 66% | 69% | 8% |

Without compaction (`--no-compact`):

| Phase | D3D12 | Vulkan | CUDA/OptiX |
| --- | --- | --- | --- |
| Create BLASes | 10 | 150 | 4 |
| Sizes, record, submit | 12 | 11 | 100 |
| Wait for GPU | 164 | 173 | 70 |
| Release BLASes | 3 | 181 | 4 |
| **Total** | **190** | **510** | **180** |

On D3D12 the uncompacted 33 KB BLASes are cheap and the compacted 14 KB ones are expensive.
That is the 32 KB committed-resource threshold, not compaction as such.

Submitting once per BLAS instead of batching leaves the D3D12 and Vulkan totals about the same
(589 and 865 ms): submit cost goes up by about 180 ms on D3D12 and 100 ms on Vulkan, and the
measured GPU wait goes down by a similar amount because the builds overlap with recording.

Cost is linear in the BLAS count from 100 to 1000 on D3D12 and Vulkan. CUDA is linear up to 500;
at 1000 the submit phase jumps from about 15 ms to about 100 ms.

Effect of mesh size, 500 BLASes, batched, with compaction:

| Triangles per mesh | D3D12 (ms) | Vulkan (ms) | CUDA/OptiX (ms) |
| --- | --- | --- | --- |
| 64 | 389 | 381 | 81 |
| 512 | 265 | 430 | 108 |
| 4096 | 203 | 486 | 188 |

D3D12 gets faster as meshes grow because more of the BLAS buffers exceed 32 KB. At 4096
triangles both the uncompacted and compacted BLASes are placed, and creation drops to 13 ms and
6 ms for 500.

## D3D12 hot spots

BLAS storage of 32 KB or less is created as a committed resource, and that is where the time
goes.

The Superluminal capture was taken with an earlier benchmark configuration (per-mesh geometry
and scratch buffers, one submit per BLAS, 3 iterations of 1000 BLASes). The BLAS storage path
is the same in both configurations, so only those rows are listed.

| Phase | Time (ms) | Where it goes |
| --- | --- | --- |
| Create BLAS storage | 813 | `D3D12MA::AllocatorPimpl::AllocateCommittedResource` 753 for 3000 compacted BLASes; `BlockVector::CreateResource` (placed) 42 for 3000 uncompacted ones |
| Release BLAS buffers | 866 | `executeDeferredDeletes`, `D3D12MA::Allocation::ReleaseThis` releasing committed resources |
| Record and submit builds | 808 | `ExecuteCommandLists` 525 inside the NVIDIA driver, `CommandEncoderImpl::finish` 135 |

- `AllocatorPimpl::PrefersCommittedAllocation` in `external/d3d12ma/D3D12MemAlloc.cpp` returns
  true for buffers up to 32 KB (half the 64 KB placement alignment).
- Inside `CreateCommittedResource`, about 70% of the time is a blocking wait:
  `CDevice::MakeResident_Worker` waits on a dxgkrnl paging operation for each new heap.
- The placed path is about 18 times cheaper per BLAS.
- `ExecuteCommandLists` costs about 0.18 ms per call, which only matters when submitting once
  per BLAS.

## D3D12 change

Three changes are in the working tree (uncommitted):

- `src/d3d12/d3d12-device.cpp`: the allocator is created with
  `D3D12MA::ALLOCATOR_FLAG_DONT_PREFER_SMALL_BUFFERS_COMMITTED`, so small buffers are
  sub-allocated from D3D12MA's heap blocks instead of becoming committed resources.
- `CMakeLists.txt`: `slang-rhi-d3d12ma` now compiles against the Agility SDK headers. Before, it
  compiled against the Windows SDK `d3d12.h` (`D3D12_SDK_VERSION` 2 here), which compiled
  D3D12MA's tight alignment support out. With the Agility SDK 1.619 headers D3D12MA detects
  `D3D12_FEATURE_D3D12_TIGHT_ALIGNMENT` at run time and uses it automatically.

- `src/d3d12/d3d12-resource.cpp`: `D3D12Resource::initCommitted` no longer forces a 64 KB
  alignment on MSAA and planar-format textures when tight alignment is in use. D3D12MA adds
  `D3D12_RESOURCE_FLAG_USE_TIGHT_ALIGNMENT` to those resources, and creation fails if an
  explicit alignment is set as well. Without this, seven D3D12 texture tests failed.

All 348 D3D12 tests in `slang-rhi-tests` pass with the three changes (RelWithDebInfo, Agility
SDK runtime loaded, tight alignment active). The path without tight alignment was only
exercised by the benchmark, not by the test suite.

Tight alignment is only active when the application loads the Agility SDK runtime, that is when
the executable exports `D3D12SDKVersion` (`SLANG_RHI_EXPORT_AGILITY_SDK`), and when the driver
supports it. On this machine (driver 617.42) it is supported. Without it, the flag still applies
and small buffers are placed with 64 KB alignment.

1000 BLASes of 512 triangles, batched, with compaction, after the change with tight alignment
active (ms):

| Phase | Before | After |
| --- | --- | --- |
| Create uncompacted BLASes | 9 | 12 |
| Create compacted BLASes | 275 | 16 |
| Sizes, record, submit | 20 | 20 |
| Wait for GPU | 180 | 195 |
| Release BLASes | 105 | 6 |
| **Total** | **590** | **250** |

Time and memory for 4000 BLASes, comparing the three allocator configurations. GPU memory is
the growth in process GPU memory while everything is alive, including geometry and scratch; it
moves in 64 MB heap-block steps.

| Configuration | Create BLASes (ms) | Release BLASes (ms) | BLAS total (ms) | GPU memory (MB) |
| --- | --- | --- | --- | --- |
| Before: small buffers committed | 1080 | 540 | 2510 | 530 |
| Flag only, no tight alignment | 80 | 37 | 955 | 724 |
| Flag with tight alignment | 190 to 540 | 25 to 95 | 1200 to 1600 | 404 |

- Without tight alignment the flag is the fastest option but uses about 37% more GPU memory
  than before for this workload, because every BLAS of 64 KB or less occupies 64 KB.
- With tight alignment memory drops below the old behaviour. Allocation is slower and noisier
  than with 64 KB alignment at 4000 BLASes, though still half the old total. I did not
  investigate why; at 1000 BLASes it is stable at about 250 ms.

## D3D12 versus CUDA/OptiX after the change

After the D3D12 change both backends are bound by the GPU build, at about 0.2 ms per BLAS, and
end up within about 17% of each other.

Superluminal captures of the RelWithDebInfo benchmark, 1000 BLASes of 512 triangles, batched,
with compaction, 5 iterations. Times are main-thread wall time per iteration in ms.

| Phase | D3D12 | CUDA/OptiX | Notes |
| --- | --- | --- | --- |
| Geometry upload | 14 | 14 | About 9 of it is the benchmark concatenating mesh data |
| Query sizes | 12 | 2 | D3D12: `NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx` |
| Create BLAS storage | 29 | 8 | D3D12: `CreatePlacedResource` 11, `D3D12MA::BlockVector::Allocate` 12 |
| Record and submit builds | 21 | 103 | D3D12: `NvAPI_D3D12_BuildRaytracingAccelerationStructureEx` 13. CUDA: 80 blocked inside `optixAccelBuild` |
| Wait for GPU | 189 | 82 | D3D12 sleeps in `WaitForSingleObjectEx`; CUDA spins in `cuStreamSynchronize` |
| Record and submit compaction | 3 | 20 | CUDA: 12 blocked inside the OptiX compact call |
| Release BLASes | 6 | 8 | |
| Release geometry | 2 | 0 | |
| **Total** | **278** | **238** | |

- Build plus wait is 213 ms on D3D12 and 205 ms on CUDA. The two APIs split it differently:
  D3D12 queues everything and waits at the end, OptiX blocks partly inside the build call.
- CPU time actually executing is 72 ms per iteration on D3D12 and 144 ms on CUDA, because CUDA
  spins while waiting.
- The remaining D3D12-specific CPU cost is about 60 ms per 1000 BLASes: storage creation 29,
  size queries 12, recording 18. CUDA spends about 30 ms on the same steps.

What is left is mostly GPU time per build, which the CPU profile cannot break down further.

## All three backends with VMA

With VMA on Vulkan (`origin/dev/skallweit/vma` at 45d4f8d9) plus the D3D12 change, all three
backends take 210 to 235 ms for 1000 BLASes, and Vulkan is now the fastest.

1000 BLASes of 512 triangles, batched, with compaction, RelWithDebInfo. Median of 10 iterations
over two runs, in ms. Geometry upload and release excluded.

| Phase | D3D12 | Vulkan | CUDA/OptiX |
| --- | --- | --- | --- |
| Query sizes | 1.2 | 1.1 | 2.0 |
| Create uncompacted BLASes | 11.2 | 1.7 | 3.6 |
| Record and submit builds | 12.6 | 11.4 | 100.5 |
| Wait for build | 173.7 | 179.2 | 76.8 |
| Create compacted BLASes | 15.7 | 1.6 | 3.2 |
| Record and submit compaction | 3.0 | 2.6 | 27.6 |
| Wait for compaction | 9.6 | 9.5 | 2.4 |
| Release BLASes | 5.5 | 1.8 | 6.9 |
| **Total** | **233** | **209** | **223** |
| Allocation and release | 32 | 5 | 14 |

- Vulkan went from about 850 ms to 209 ms. Creating 2000 BLAS buffers dropped from about 190 ms
  to 3 ms and releasing them from about 400 ms to 2 ms.
- Record, submit and wait add up to 199 ms on D3D12, 203 ms on Vulkan and 207 ms on CUDA. The
  GPU build is the same cost on all three.
- D3D12 now has the highest allocation cost, at about 13 microseconds per BLAS to create
  against about 2 on Vulkan.

## What is left to gain

The remaining time is a fixed GPU cost of about 0.17 ms per build, independent of triangle
count. The only large lever left is building fewer, larger BLASes.

Measured on the VMA commit with the D3D12 change, 1000 meshes of 512 triangles, batched,
RelWithDebInfo, median of 5 iterations, total BLAS time in ms:

| Variant | D3D12 | Vulkan | CUDA/OptiX |
| --- | --- | --- | --- |
| Baseline: 1000 BLASes, PreferFastTrace, compaction | 239 | 211 | 221 |
| PreferFastBuild | 227 | 197 | 204 |
| No compaction | 187 | 176 | 176 |
| PreferFastBuild, no compaction | 177 | 161 | 161 |
| 10 meshes per BLAS (100 BLASes) | 45 | 36 | 41 |
| 100 meshes per BLAS (10 BLASes) | 13 | 8 | 10 |
| 100 meshes per BLAS, PreferFastBuild, no compaction | 7 | 6 | 7 |

- **Merging meshes into one BLAS as separate geometries** cuts the time by about 5 times at 10
  per BLAS and about 20 times at 100 per BLAS, for the same 512,000 triangles. The cost is that
  all geometries in a BLAS share one instance transform and are rebuilt together, so it fits
  static geometry.
- **Skipping compaction** saves about 20%. It removes a second allocation, a copy and a wait
  per BLAS.
- **PreferFastBuild** saves 5 to 8%.
- Dropping from 512 to 64 triangles per mesh only reduces the GPU wait from about 180 ms to
  about 130 ms, which confirms the per-build fixed cost.

Superluminal captures (25 iterations per backend, 8190 Hz, the profiler's maximum) of the
remaining CPU-side work, per 1000 BLASes:

| Backend | Phase | ms | Breakdown |
| --- | --- | --- | --- |
| D3D12 | Create BLAS storage (2000 buffers) | 28 | `CreatePlacedResource` 10, D3D12MA TLSF search 6, new 64 MB heap blocks 5, `GetResourceAllocationInfo` 2.5 |
| D3D12 | Record builds | 15 | NVAPI build call 10, compacted-size copy and barriers per build 1.7 |
| Vulkan | Record builds | 13 | Driver build call 8, barriers 1.5, size query 2 |
| Vulkan | Create BLAS storage | 4 | |
| CUDA | Create and release BLAS storage | 15 | `cuMemAlloc` and `cuMemFree` per BLAS |
| CUDA | Wait | 82 | Spinning in `cuStreamSynchronize` |

- The D3D12 heap-block cost is partly a benchmark artifact: D3D12MA frees empty blocks when an
  iteration releases everything and recreates them in the next one.
- The per-build barriers are not what serialises the GPU. With the per-build scratch barrier
  removed in a test build, the GPU wait was unchanged on D3D12 and Vulkan. That change is
  reverted.
- Realistic slang-rhi-side gains are small: about 15 ms on D3D12 (only through placing BLASes
  in a shared buffer) and about 10 ms on CUDA (sub-allocating BLAS storage from the device heap
  as buffers already do).

## OptiX failure with many tiny BLASes

On CUDA, `optixAccelBuild` intermittently returns `OPTIX_ERROR_INVALID_VALUE` when many builds
of tiny meshes with `AllowCompaction` are queued on the stream without a synchronisation in
between. The arguments of the failing call are valid; I could not find a slang-rhi misuse.

| Configuration (batched, with compaction unless noted) | Failing runs |
| --- | --- |
| 1000 BLASes of 512 triangles | 0 of about 60 |
| 2000 BLASes of 512 triangles | 0 of 10 |
| 1000 BLASes of 64 triangles, 6 iterations | about 10% |
| 2000 BLASes of 64 triangles | 29 of 30 |
| 2000 BLASes of 16 triangles | 10 of 10 |
| 1000 to 4000 BLASes of 64 triangles, no compaction | 0 of 50 |
| 4000 BLASes of 16 triangles, stream synchronised every 64 builds | 0 of 10 |

What the investigation established:

- At the failing call the scratch and output buffers are 128-byte aligned, large enough
  according to a fresh `optixAccelComputeMemoryUsage`, and do not overlap anything.
- It does not depend on scratch layout (shared or per BLAS), geometry layout (shared or per
  mesh), batching, or mesh data (identical meshes fail too).
- It needs the `AllowCompaction` build flag. Requesting the compacted size is not required;
  the flag alone is enough.
- Failures land on recurring build indices spaced about 251 apart (for example 1262 and 1513),
  and only when more than roughly 110 to 230 builds have been issued since the last stream
  synchronisation. That pattern suggests a fixed-size internal pool in OptiX, but that is an
  inference.
- Calling `cuStreamSynchronize` every 64 or 128 builds avoided it in every run; every 256 or
  more did not. Synchronising every 64 builds cost 0 to 5% in the benchmark, within noise.

A workaround in the CUDA backend would be to synchronise the stream after every 64
acceleration structure builds. Not implemented. Tested with driver 617.42 on a TITAN RTX; other
drivers and OptiX versions were not tested.

When it happens the benchmark prints `OPTIX_ERROR` and no timing table.

## Vulkan hot spots

The capture and numbers in this section predate VMA and describe the old per-buffer
`vkAllocateMemory` path.


Every BLAS buffer gets its own `vkAllocateMemory`, and freeing those allocations costs twice as
much as creating them.

Same capture configuration as D3D12.

| Phase | Time (ms) | Where it goes |
| --- | --- | --- |
| Release BLAS buffers | 2181 | `~BufferImpl` into the driver: `DxgkDestroyAllocation2` about 940, `DxgkFreeGpuVirtualAddress` about 765 waiting on VidMm fences |
| Wait for GPU | 816 | Blocked in the driver |
| Create BLAS storage | 617 | `allocateVkMemory` 592 |

- `VKBufferHandleRAII::init` in `src/vulkan/vk-buffer.cpp` calls `vkCreateBuffer`,
  `vkAllocateMemory` and `vkBindBufferMemory` for every buffer. Each is a separate WDDM
  allocation.
- VMA is vendored in `external/vma` and compiled as `slang-rhi-vma`, but I found no call into
  it from the Vulkan backend.
- Per-buffer allocation also counts against the driver's `maxMemoryAllocationCount` limit, so a
  scene with a few thousand BLASes could fail outright. Not tested.

## CUDA/OptiX hot spots

Allocation is not a hot spot. `createAccelerationStructure` calls `cuMemAlloc` directly, at
about 4 microseconds per BLAS.

| Phase | Time (ms) | Where it goes |
| --- | --- | --- |
| Record and submit builds | 331 | `optixAccelBuild` 312, of which 234 is blocked waiting |
| Wait for GPU | 264 | `cuStreamSynchronize`, spinning on the CPU |

The jump in submit time at 1000 BLASes is `optixAccelBuild` blocking. Cause not found; possibly
back-pressure from queued builds (unverified).

## Proposals

Only the first has a measured gain; the others are estimates from the per-call costs above.

| # | Proposal | Backend | Expected gain at 1000 small BLASes | Effort |
| --- | --- | --- | --- | --- |
| 1 | Stop creating small buffers as committed resources | D3D12 | Done, see "D3D12 change": 590 to 250 ms | Done |
| 2 | Let a BLAS live at an offset inside a shared buffer | All | Estimate: removes per-BLAS allocation and release; D3D12 to about 200 ms, Vulkan to about 250 ms | Medium, public API change or internal pool |
| 3 | Sub-allocate buffer memory with VMA | Vulkan | Estimate: removes most of the 190 ms create and 400 ms release | Medium |
| 4 | Block in `waitOnHost`, look at the 1000-build stall | CUDA | Small | Small |

1. **D3D12: small buffers.** Implemented. Open question: whether to keep the flag when tight
   alignment is unavailable, where it trades about 37% more GPU memory for small-BLAS workloads
   against a 2.6 times faster setup.
2. **BLAS storage inside a buffer.** All three APIs accept an acceleration structure at an
   offset in a larger buffer (`VkAccelerationStructureCreateInfoKHR::offset`, a D3D12 GPU
   virtual address, an OptiX device pointer). Either add an optional buffer and offset to
   `AccelerationStructureDesc`, mirroring `BufferDesc::heap`, so the renderer can bump-allocate
   BLAS storage the way it already does for scratch, or keep the API and have each device pool
   acceleration structure storage in blocks of a few megabytes. On D3D12 this avoids the 64 KB
   alignment of proposal 1, since alignment inside a buffer is 256 bytes. This is the option
   that fixes D3D12 and Vulkan together.
3. **Vulkan: VMA.** Route `VKBufferHandleRAII::init` through VMA so buffers share large device
   memory blocks. Shared and externally exported buffers keep dedicated allocations. Less
   targeted than proposal 2, but helps every small buffer and removes the allocation-count
   limit risk.
4. **CUDA.** `waitOnHost` spins in `cuStreamSynchronize`; a blocking sync mode would free the
   core. The `optixAccelBuild` stall at 1000 queued builds needs a closer look first.

Not covered here: the benchmark can also create one geometry buffer pair and one scratch buffer
per BLAS (`--geometry per-mesh --scratch per-blas`). Those paths have their own costs on D3D12
and Vulkan, but they do not apply to a renderer that pools both.

## How to reproduce

```
cmake --preset msvc
cmake --build build --config RelWithDebInfo --target blas-benchmark

build\RelWithDebInfo\blas-benchmark.exe --device d3d12 --strategy batched --count 1000
build\RelWithDebInfo\blas-benchmark.exe --device vulkan --strategy batched --count 1000 --no-compact
build\RelWithDebInfo\blas-benchmark.exe --device cuda --iterations 5 --csv results.csv
```

Options: `--device d3d12|vulkan|cuda`, `--count`, `--tris`, `--strategy per-blas|batched`,
`--geometry shared|per-mesh`, `--scratch shared|per-blas`, `--no-compact`, `--fast-build`,
`--geometries-per-blas N`, `--same-mesh`, `--iterations`,
`--csv`. Each phase is a separate non-inlined function, so it shows up by name in a profiler.

To capture a profile, run from an elevated prompt:

```
SuperluminalCmd.exe run windows --capture-path d3d12.etl --resolve ^
  --symbol-location build\RelWithDebInfo ^
  build\RelWithDebInfo\blas-benchmark.exe --device d3d12 --count 1000 --iterations 3
```

Profiler times are up to 25% higher than the unprofiled timings in Results.

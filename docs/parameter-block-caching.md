# Parameter-block caching

Finalized shader objects bound as `ParameterBlock<T>` reuse backend preparation
throughout one command-buffer recording. Mutable roots can change constants,
output bindings, programs, and passes while sharing a finalized scene. Native
binding commands, resource transitions, UAV dependencies, and Metal residency
declarations still occur at each consuming use where required.

## Finalization and lifetime

`finalize()` freezes ordinary data, bindings, specialization arguments, children,
and root entry points. Existing handles to shared descendants cannot mutate them.
The checks apply with and without the debug layer. Referenced buffer, texture, and
acceleration-structure contents remain mutable under their normal synchronization
contracts. The pointer returned by `reserveData()` must be filled immediately and
must not be retained for mutation after finalization.

Preparation remains lazy. Each command buffer retains cached objects internally,
preventing identity reuse and retaining their resource graphs. Frozen resource
graphs are traversed once per recording. Generated backend resources remain in
the existing command-buffer pools or internal ownership lists. Reset clears the
cache alongside those allocations. Distinct command buffers never share transient
payloads. See [object lifetime](object-lifetime.md) for internal ownership rules.

## Layout and attachment

Cache keys combine retained object identity with a structural effective-layout
identity. Ordinary packing, binding types/counts/order, relative offsets, native
layout requirements, and nested layouts participate. Type names and consuming
program identity do not determine compatibility.

Layout descriptions are interned on the first use of each layout in a recording,
then memoized by retained layout pointer. Repeated lookups use small integer
identities and never revisit reflection or hash object contents. This keeps layout
memoization local to the recording without shared locks or new mutable state on
shader objects. Finalized specialization arguments are collected when freezing.

Preparation publishes an entry only after success. Attachment relocates the small
binding records to the consuming root. Resource-use lists are referenced through
prepared-subtree links rather than copied into every root.

| Backend | Reused representation | Per-use attachment |
| --- | --- | --- |
| Vulkan | Descriptor sets, ordinary buffers, nested blocks, push-constant data, resource-use lists | Set handles at the current set numbers; relative mappings also support sparse synthetic sets |
| D3D12 | Descriptor tables, constant allocations, relative root parameters, resource-use lists | Root descriptors and table handles at current root-parameter indices |
| Metal | Argument buffers, nested buffers, retained pointer resources, residency lists | Argument-buffer slot and native-encoder residency declarations |
| WebGPU | Bind groups and uniform storage | Group handles at current group indices |
| D3D11 | Constant allocations and relative resource arrays | Native slots at reflected offsets |
| CUDA | Packed host/global-memory blocks and child pointers | Block pointer in the root; existing pool upload ordering |
| CPU | Packed blocks and child pointers | Block pointer in the root |

WebGPU includes the compiled group schemas and stage visibility in its effective
layout identity. It retains separate variants where programs use different native
layouts. Vulkan can share descriptor storage across ordinary and synthetic-resource
programs; synthetic resources still require independent sets.

Slang can report a declared parameter block without an owned descriptor set as a
constant-buffer binding. Layout metadata preserves that declared boundary, so an
empty parent can reuse its nested blocks without introducing a new native set.
D3D11 also preserves this distinction for resource-only parameter blocks.

Whole-root caching, mutable-object version caching, arbitrary constant-buffer
caching, and reuse across command buffers remain outside this implementation.

## Validation and benchmarks

`test-parameter-block-cache.cpp` checks preparation counts as well as shader output:
compatible programs at different placements, distinct effective layouts, repeated
child references, empty parents, mutable descendants, independent recordings and
recycling, released application references, copies, UAV dependencies, indirect
dispatch, compute/ray-tracing sharing, sparse Vulkan sets, and finalization checks.
Ray tracing uses feature checks; CPU uses direct dispatch because its indirect
dispatch method is unavailable.

The pinned Slang 2026.17.1 compiler rejects arrays of `ParameterBlock<T>` as
non-addressable types before RHI layout construction. Repeated named blocks and
existing resource-array tests are covered; native parameter-block arrays require
compiler support. This change does not alter ordinary shader layouts to work
around it. The benchmark uses named resource fields because this compiler also
rejects resource arrays when targeting WGSL.

`benchmark-parameter-block-cache` in `test-benchmark-command.cpp` compares mutable
and finalized 16-KiB scenes (1024 four-component constants plus four buffer
bindings), using 512 dispatches, one or two alternating programs, and 1, 64, or 512
distinct objects. Both pipelines and recording pools are warmed before each
measurement. It reports recording, finish, submit, and host-wait times separately,
including work deferred to submit by CPU/CUDA/D3D11.

Instrumentation reports successful cached preparations, hits, ordinary bytes
packed, logical bytes prepared for GPU upload, arena allocation calls/bytes/new
pages, and process memory retained between recording start and finish. Upload
bytes exclude allocator padding and backend batching; CPU has no GPU uploads.
Process memory includes allocator/driver effects and is not an exact GPU-memory
counter. Instrumentation is optional and local to the recording thread.

Run performance tests in Release without validation; use Debug for correctness:

```powershell
cmake --build build --config Release --target slang-rhi-tests
build/Release/slang-rhi-tests.exe -tc="benchmark-parameter-block-cache*"
cmake --build build --config Debug --target slang-rhi-tests
build/Debug/slang-rhi-tests.exe -tc="parameter-block-*"
```

Local validation covers Windows CPU, CUDA, D3D11, D3D12, Vulkan, and WebGPU.
Metal implementation and native-encoder residency changes require macOS CI or a
Mac for compilation and execution. Local Vulkan Debug runs use SDK 1.4.321.1 with
only `VUID-VkDeviceCreateInfo-pNext-pNext` filtered via
`VK_LAYER_MESSAGE_ID_FILTER`, because the installed layer predates device
extensions enabled by the current headers/driver. Binding and synchronization
validation remain enabled.

### Local results

On Windows with a Ryzen 9 3900X, NVIDIA TITAN RTX, driver 617.42, and the pinned
Slang 2026.17.1, 200 selected Debug regression cases passed (34,464 assertions),
including the large-scene D3D11 benchmark. Unsupported ray-tracing cases used
feature skips. The Release benchmark passed on all six available backends in
three runs. Metal was not built or run locally.

The large-scene benchmark also found and fixed an existing D3D11 bug: constant
buffer window sizes were expressed in bytes rather than 16-byte constants. The
correct count is rounded to a multiple of 16 constants, as required by
[`SetConstantBuffers1`](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-cssetconstantbuffers1).

The following are median recording times in milliseconds across the three Release
runs. Every cell is **mutable / finalized**; each recording contains 512 dispatches.
These compare the two paths in the new implementation, not a before/after baseline
of mutable-path performance.

| Backend | 1 block, 1 program | 1 block, 2 programs | 64 blocks, 2 programs | 512 blocks, 2 programs |
| --- | ---: | ---: | ---: | ---: |
| CPU | 2.051 / 0.360 | 2.433 / 0.363 | 2.248 / 0.682 | 2.947 / 5.306 |
| CUDA | 1.165 / 0.363 | 1.289 / 0.486 | 1.833 / 0.580 | 1.842 / 3.578 |
| D3D11 | 8.389 / 0.716 | 8.020 / 0.802 | 2.820 / 1.270 | 3.724 / 4.845 |
| D3D12 | 1.573 / 0.854 | 2.009 / 0.872 | 2.312 / 0.986 | 2.646 / 2.662 |
| Vulkan | 2.499 / 0.678 | 2.387 / 0.934 | 2.409 / 1.129 | 2.988 / 3.788 |
| WebGPU | 5.750 / 2.639 | 5.312 / 2.153 | 5.346 / 2.550 | 6.262 / 7.087 |

Repeated scenes reduce recording time. Single-use finalized scenes have no hits
and can cost more due to cache insertion, retained-object bookkeeping, and prepared
binding records. Finalizing single-use objects is not a general performance win.
For 1, 64, and 512 finalized objects, all backends report respectively
1/511, 64/448, and 512/0 preparations/hits, with either one or two programs.

Remaining phases for **1 block and 2 programs**, also median milliseconds and
mutable / finalized:

| Backend | Finish | Submit | Host wait |
| --- | ---: | ---: | ---: |
| CPU | 0.017 / 0.005 | 0.021 / 0.014 | 0.000 / 0.000 |
| CUDA | 0.007 / 0.006 | 3.720 / 2.845 | 3.967 / 0.854 |
| D3D11 | 0.071 / 0.011 | 0.255 / 0.241 | 7.580 / 14.180 |
| D3D12 | 0.720 / 0.709 | 0.039 / 0.051 | 2.986 / 2.210 |
| Vulkan | 0.669 / 0.650 | 0.067 / 0.056 | 2.575 / 1.501 |
| WebGPU | 19.519 / 1.207 | 3.212 / 2.448 | 0.019 / 0.013 |

Host waits depend on how much GPU work overlaps CPU recording. In particular,
D3D11's faster recording moves more of the remaining work into the wait. WebGPU
finish times include native validation and driver work even in a Release build.
These are short local measurements, not GPU execution-time or frame-rate claims.

Work and memory for the same repeated case, **mutable / finalized**. Ordinary and
upload sizes include mutable root data; arena figures exclude native descriptors,
driver allocations, and the cache's standard-library containers.

| Backend | Ordinary bytes packed | Logical upload bytes | Arena allocation calls | Arena bytes |
| --- | ---: | ---: | ---: | ---: |
| CPU | 8,437,760 / 32,832 | 0 / 0 | 6,656 / 6,145 | 8,522,752 / 117,824 |
| CUDA | 8,437,760 / 32,832 | 8,437,760 / 32,832 | 5,120 / 5,120 | 93,184 / 93,184 |
| D3D11 | 8,390,656 / 18,432 | 8,390,656 / 18,432 | 4,608 / 4,609 | 822,272 / 823,744 |
| D3D12 | 8,390,656 / 18,432 | 8,390,656 / 18,432 | 6,144 / 6,659 | 248,832 / 257,232 |
| Vulkan | 8,390,656 / 18,432 | 8,390,656 / 18,432 | 7,680 / 8,198 | 269,312 / 277,752 |
| WebGPU | 8,396,800 / 24,576 | 8,396,800 / 24,576 | 5,120 / 5,120 | 84,992 / 84,992 |

Median process-memory deltas from recording start to finish were CPU
9,494,528 / 1,056,768 bytes, D3D11 11,001,856 / 1,060,864 bytes, and WebGPU
26,988,544 / 9,457,664 bytes. CUDA, D3D12, and Vulkan deltas were within 64 KiB
of zero in both paths because their pools and allocators reuse existing storage;
negative deltas reflect other allocator/driver releases. These deltas do not
measure total retained GPU memory or prove that native allocation costs are zero.

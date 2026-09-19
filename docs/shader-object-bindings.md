# Finalized shader object bindings

`IShaderObject::finalize()` freezes a shader object and enables persistent backend bindings. Reusing a finalized root avoids reconstructing its binding data on every draw or dispatch. A mutable root can also reference finalized parameter blocks, allowing material or scene bindings to be reused while per-dispatch values change.

## Usage

Populate an object completely before finalizing it:

```cpp
auto material = root->getObject(ShaderCursor(root)["material"].m_offset);
SLANG_RETURN_ON_FAIL(material->finalize());

// The root remains mutable. Reuse its finalized material across dispatches.
ShaderCursor(root)["iteration"].setData(iteration);
pass->bindPipeline(pipeline, root);
pass->dispatchCompute(1, 1, 1);
```

If the whole binding graph is constant, finalize the root instead. Keep the shader object across command buffers to benefit from persistent reuse. Creating and finalizing a new object for every dispatch still pays preparation costs.

Finalization recursively freezes subobjects and root entry points, including children shared with other roots. All writes through `reserveData()` pointers must finish before finalization; those pointers must not be used to modify a finalized object. Data, object, binding, and specialization setters reject subsequent modifications. Finalization itself must finish before sharing an object for concurrent recording.

The contents of bound buffers and textures can still change. Finalization freezes binding identities, ranges, and uniform values, rather than the resources' contents. Resource state tracking, UAV dependencies, and Metal residency declarations still apply on reuse.

## Implementation

Each finalized object owns immutable preparation records keyed by the effective specialized layout and the kind of prepared data, with storage-specific context where needed. Preparation is lazy: the first use pays for native allocation and serialization. A per-object lock serializes concurrent first use, and a failed preparation is not published. Specialized layouts, specialization arguments, and the resource ownership graph are also retained for reuse.

Block preparation and root assembly are separate operations. `prepareParameterBlock()` produces immutable bindings in block-relative coordinates. `composeParameterBlock()` places those bindings into the enclosing root or block. D3D11 uses the analogous constant-buffer operations. Changing a block's root position does not add a preparation-cache entry for the same effective layout. Native layout compatibility still comes from that effective layout; distinct layouts may require separate prepared records.

Command buffers retain preparation records independently of the original shader objects. Records own their native allocations and retain referenced resources and child records, so releasing the shader objects after recording does not invalidate submitted work. Native allocations are reclaimed when both shader-object and command-buffer references are gone, using the existing command-buffer retirement mechanism.

| Backend | Persistent root data | Reuse inside mutable roots |
| --- | --- | --- |
| D3D12 | Root parameters, GPU descriptor ranges, constant buffers, resource-use records | Compact block-relative root-descriptor and descriptor-table ranges, plus constant buffers |
| Vulkan | Descriptor sets, uniform buffers, push-constant data, resource-use records | Parameter-block descriptor sets, uniform buffers, and relative push-constant bindings |
| WebGPU | Bind groups and uniform buffers | Parameter-block bind groups using their effective layout's native bind-group layouts |
| Metal | Binding arrays, constant/argument buffers, residency records | Argument buffers and ordinary-data buffers |
| D3D11 | Binding arrays and constant buffers | Constant/parameter-block binding arrays rebased during composition |
| CPU | Serialized parameter graphs | Serialized constant/parameter-block subgraphs |
| CUDA | Serialized host/device parameter graphs | Persistent device parameter-block storage |

D3D12 descriptor allocations use the existing device descriptor heaps with persistent ownership. Vulkan uses the existing device descriptor allocator, with allocation and freeing protected by a mutex. Command-buffer transient allocators continue to serve mutable data.

D3D12, Vulkan, and Metal suballocate finalized uniform data from device-owned, persistently mapped upload pages. Metal also uses these pages for finalized argument buffers. Each allocation is a reference-counted slice that carries its buffer, offset, and mapped address. Descriptors, direct buffer bindings, and nested Metal argument-buffer addresses include the slice offset. Alignment is 256 bytes, or Vulkan's uniform-buffer offset alignment if larger. The normal page size is 64 KiB; larger requests get dedicated pages.

Prepared records and recorded commands retain slices independently. A slice returns to the pool only after its last reference is released, so recording or submitting another command buffer cannot overwrite live uniform data. Allocation and freeing are protected by a per-device mutex. Freed ranges coalesce for reuse; empty pages are released down to a 64 KiB retention budget, and oversized pages are released when empty. Live slices keep the device alive, while idle page buffers use weak device references to avoid an ownership cycle. Pool statistics expose live bytes, capacity, allocation count, and cumulative page creations for testing.

D3D12 and Vulkan process resource uses even when binding-data pointers match the previous command. A cache hit can suppress redundant native bindings without suppressing required synchronization.

Vulkan prepares push-constant bytes with relative range indices. Composition adjusts those indices; the completed root resolves the pipeline's byte offsets and stage visibility. Blocks containing push constants can therefore use persistent descriptor preparation. CUDA retains the existing per-launch update of module-global parameters.

All backend builders take an explicit `BindingDataStorage` context. The shared context handles CPU allocations and resource retention; backend extensions handle uniform storage and native bindings. Each context borrows storage from either a command buffer or a prepared record. Destroying the context releases nothing: the owner keeps the allocations alive until command-buffer retirement or the last prepared-record reference is released. Failed preparations release their partially allocated records without publishing a cache entry.

Transient and persistent lifetimes are explicit, including on backends that do not use the shared uniform arena. Persistent contexts reject mutable uniform snapshots. Preparation creates a fresh builder with the prepared record's context instead of copying a builder and replacing allocator pointers.

| Backend | Storage context responsibilities |
| --- | --- |
| D3D12 / Vulkan | Existing transient uniform arenas, pooled finalized uniform slices, and native descriptor allocations |
| D3D11 | Existing transient constant-buffer pool and finalized constant buffers |
| WebGPU | Existing constant-buffer pool, finalized uniform buffers, and ownership of new or reused bind groups, including partial preparation cleanup |
| Metal | Pooled finalized ordinary and argument-buffer slices, transient buffers, and retention of buffers resolved for pointer residency |
| CPU | Host parameter allocations and retention of prepared subgraphs |
| CUDA | Existing transient parameter pool, persistent host/device parameter storage, and stream-ordered resource tracking for mutable roots |

CUDA uploads transient parameters through the command buffer's pool and updates module globals during submission. Persistent device parameter buffers are initialized after their host graph is assembled, before the cache publishes them. Metal retains its existing residency declarations. D3D11 and WebGPU still allocate individual finalized uniform buffers: their existing update paths require different pooling strategies from persistently mapped upload pages. CPU and CUDA retain their existing host/device parameter allocation paths.

Persistent storage trades memory and first-use cost for lower repeated CPU cost. Preparation records remain until the object and recorded commands release them. The pool bounds empty-page retention; it does not limit live prepared data or evict records. Partially occupied pages remain allocated until their last slice is released. Automatic revision caching for mutable objects and dynamic-offset bindings remain possible follow-ups.

## Benchmark

Build and run an optimized configuration:

```powershell
cmake --build build --config Release
./build/Release/slang-rhi-tests.exe '-tc=benchmark-finalized-shader-object*'
```

The benchmark compares five modes: mutable bindings, a finalized block in a mutable root, a finalized root, a changing mutable root, and a changing mutable root with a finalized block. The last two update one root uniform every dispatch and use their own baseline.

Each mode runs with 1 and 1,000 dispatches per command buffer. It discards one warmup, rotates mode order, and reports the median of seven samples. Objects persist across samples and command buffers. The shader reads four structured-buffer bindings and uniforms from a parameter block, root, and entry point, and accumulates into a UAV. Every sample checks the result after GPU completion.

Output lines beginning with `binding-benchmark,` report nanoseconds per dispatch for encoding, finishing, submission, and their total, plus speedups against the corresponding mutable baseline. The total is measured directly; separately reported component medians need not sum to it. GPU waiting and readback are outside timing. Object creation, finalization, pipeline creation/binding, and initial encoder setup are also outside timing. The benchmark measures warm CPU costs, not GPU execution speed or first-use amortization. Single-dispatch figures include command-buffer overhead and can be dominated by timer granularity or transient-pool setup. Use Release: Debug builds always enable validation.

`benchmark-finalized-shader-object-first-use` instead binds 64 or 1,024 distinct finalized roots once each. It reports median CPU encoding time per object, including pipeline binding and dispatch, and the additional live RHI resource count after recording (`binding-first-use,`). Object creation/finalization, finishing, submission, GPU waiting, and readback are outside timing. Each size discards one warmup and measures seven samples. Objects have different uniform values, and every sample releases them before submission and checks the GPU result. Resource counts include supporting buffers; they are not descriptor counts or native allocation bytes. This benchmark measures first preparation with warmed device allocators, rather than device startup.

### Example measurement

Measured on Windows with MSVC Release, NVIDIA TITAN RTX, 2026-09-19. These are observations on one machine, not performance requirements. The mutable baseline uses the same build, so these ratios measure the benefit of opting into finalization rather than a comparison against an older revision.

For 1,000 dispatches per command buffer:

| Backend | Mutable encode (ns/dispatch) | Finalized-root encode (ns/dispatch) | Root encode speedup | Root total CPU speedup | Block encode speedup | Changing-root + block encode / total speedup |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU | 211.5 | 108.8 | 1.94x | 1.81x | 1.03x | 1.03x / 1.03x |
| CUDA | 342.7 | 90.1 | 3.80x | 1.08x | 1.49x | 1.47x / 1.03x |
| D3D11 | 676.1 | 122.9 | 5.50x | 5.82x | 1.26x | 1.14x / 1.08x |
| D3D12 | 1801.5 | 98.0 | 18.38x | 2.67x | 1.48x | 1.31x / 1.18x |
| Vulkan | 936.9 | 108.3 | 8.65x | 2.04x | 1.34x | 1.55x / 1.24x |
| WebGPU | 3452.3 | 124.1 | 27.82x | 3.44x | 1.36x | 1.08x / 0.99x |

Full-root reuse removes most binding construction in this workload. Nested-block gains are smaller because mutable root data still needs preparation. CUDA submission dominates its total, and WebGPU's changing-root case showed essentially no total CPU improvement in this run. Timing thresholds are deliberately not enforced by tests.

The preparation/composition refactor was checked against a saved Release benchmark baseline. In the 1,000-dispatch finalized-block case, encoding cost was 520.8 -> 520.8 ns on D3D11, 1272.9 -> 1275.0 ns on D3D12, and 544.3 -> 536.0 ns on Vulkan. WebGPU varied between runs: the initial after measurement was 3126.5 ns, while an isolated repeat measured 2383.7 ns against the 2594.9 ns baseline. These samples support retaining the existing performance characteristics; they do not establish a new performance guarantee. All six backend benchmark cases verified their shader outputs.

The first storage-context stage was also compared with a saved Release baseline on the same machine, using 1,000 dispatches per command buffer:

| Backend | Mode | Before encode (ns/dispatch) | After encode (ns/dispatch) |
| --- | --- | ---: | ---: |
| D3D12 | Mutable | 1864.1 | 1628.3 |
| D3D12 | Finalized block | 1420.6 | 1289.3 |
| D3D12 | Finalized root | 100.3 | 101.5 |
| Vulkan | Mutable | 1307.5 | 936.3 |
| Vulkan | Finalized block | 676.4 | 537.5 |
| Vulkan | Finalized root | 107.5 | 99.7 |

These samples show no material encoding regression. Costs also fell for mutable bindings and command-buffer finishing, so the observed reductions should not be attributed to the abstraction alone. This stage preserves allocation policy and is intended as preparation for later allocator changes. Both backend benchmarks passed all output checks (1,069 assertions).

The remaining migrations were compared with an isolated build of the pre-migration revision, using the same Release flags and dependency binaries. Three runs per binary alternated execution order; the following values are medians of the reported seven-sample medians for 1,000 dispatches:

| Backend | Mutable encode before / after (ns) | Finalized block before / after (ns) | Finalized root before / after (ns) | Changing root + block before / after (ns) |
| --- | ---: | ---: | ---: | ---: |
| CPU | 219.2 / 208.0 | 188.3 / 185.1 | 109.4 / 107.1 | 188.9 / 199.5 |
| CUDA | 370.1 / 345.4 | 229.9 / 222.3 | 94.3 / 90.2 | 243.4 / 235.9 |
| WebGPU | 4044.9 / 3430.1 | 3061.0 / 3007.4 | 123.4 / 122.5 | 3095.4 / 3093.1 |
| D3D11 (same CPU core) | 651.2 / 616.7 | 501.0 / 507.4 | 133.4 / 128.0 | 517.0 / 514.8 |

D3D11 initially showed encoding overhead from the extracted uniform-storage helper. Keeping that helper inline restored comparable encoding costs. Its final comparison pinned both processes to the same available CPU core and used high process priority to reduce scheduling variation; these settings were not used for the other rows. Total CPU time in that D3D11 comparison varied by +0.3% to +4.2% across modes. CPU's changing-root/block case was about 11 ns slower in the alternating comparison. CUDA submission times and WebGPU mutable timings varied substantially across runs, so these measurements do not establish a new speedup. Every benchmark sample verified its output. Metal performance still needs measurement on macOS.

### Persistent allocator measurement

The mapped-page implementation was compared against a saved pre-pool Release executable with the same benchmark and dependency binaries, on the Windows/TITAN RTX system described above:

| Backend | Distinct roots | First-use encode before / after (µs/root) | Speedup | Extra live resources before / after |
| --- | ---: | ---: | ---: | ---: |
| D3D12 | 64 | 502.3 / 4.7 | 106.3x | 193 / 2 |
| D3D12 | 1,024 | 552.2 / 12.3 | 44.8x | 3,073 / 13 |
| Vulkan | 64 | 134.8 / 4.8 | 28.0x | 128 / 1 |
| Vulkan | 1,024 | 155.8 / 10.4 | 15.0x | 2,048 / 8 |

Each entry is a seven-sample median after one warmup. These gains apply to first preparation of many distinct finalized objects in this workload, where individual native uniform allocations dominated. They are not GPU speedups or additional gains for already prepared roots. D3D12's extra resources after pooling are one transient upload buffer plus 1 or 12 uniform pages; Vulkan uses 1 or 8 uniform pages. Descriptor allocation policy is unchanged.

Warm behavior was checked with three runs per executable, alternating execution order. For 1,000 dispatches, median finalized-block encoding was 1,309.5 -> 1,290.2 ns on D3D12 and 574.4 -> 590.0 ns on Vulkan. Changing-root/finalized-block encoding was 1,315.3 -> 1,319.2 ns and 584.9 -> 609.9 ns, respectively. Warm timings varied between runs; these results show comparable costs, including a small increase in the Vulkan block cases. All benchmark samples checked their GPU output. Metal allocator performance still needs measurement on macOS.

## Correctness coverage

`tests/test-finalized-shader-object.cpp` covers recursive immutability, changing roots and entry points with frozen children, shared blocks at different binding positions and across different root layouts, D3D12 root-descriptor rebasing, Vulkan push constants inside finalized blocks, changing resource contents, command-buffer recycling, releasing shader objects before submission, alternating specialized pipelines, and concurrent first use on CPU, D3D12, Vulkan, and WebGPU. Benchmark samples also verify results.

Storage-lifetime cases cover every backend: they allocate backend binding storage, inject a preparation failure, and check cleanup and successful retry. They also check that destroying the borrowed context or original shader object preserves allocations retained by the prepared record, that the last record reference releases retained resources, and that persistent storage rejects mutable uniforms. WebGPU also exercises native bind-group creation and retaining an existing group; CPU and CUDA exercise serialized parameter storage. The Metal case includes pooled ordinary and argument-buffer slices.

`tests/test-persistent-buffer-pool.cpp` covers aligned non-overlapping slices, full page occupancy, freed-range reuse and coalescing, bounded empty-page retention, oversized allocations, invalid sizes, device reference ownership, and concurrent allocation/freeing with data-integrity checks. The pooled-uniform GPU lifetime case records two batches of distinct roots, releases those roots, then submits the commands in reverse order to detect premature slice recycling and missing offsets in root, block, and entry-point bindings.

The implementation was built and exercised on CPU, CUDA, D3D11, D3D12, Vulkan, and WebGPU. Metal changes require compilation and validation on macOS.

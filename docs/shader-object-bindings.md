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

Each finalized object owns immutable preparation records keyed by the effective specialized layout and any backend placement context. Preparation is lazy: the first use pays for native allocation and serialization. A per-object lock serializes concurrent first use, and a failed preparation is not published. Specialized layouts, specialization arguments, and the resource ownership graph are also retained for reuse.

Command buffers retain preparation records independently of the original shader objects. Records own their native allocations and retain referenced resources and child records, so releasing the shader objects after recording does not invalidate submitted work. Native allocations are reclaimed when both shader-object and command-buffer references are gone, using the existing command-buffer retirement mechanism.

| Backend | Persistent root data | Reuse inside mutable roots |
| --- | --- | --- |
| D3D12 | Root parameters, GPU descriptor ranges, constant buffers, resource-use records | Parameter-block descriptor tables and constant buffers; placement included in the cache key |
| Vulkan | Descriptor sets, uniform buffers, push-constant data, resource-use records | Parameter-block descriptor sets and uniform buffers |
| WebGPU | Bind groups and uniform buffers | Parameter-block bind groups, keyed by enclosing layout and placement |
| Metal | Binding arrays, constant/argument buffers, residency records | Argument buffers and ordinary-data buffers |
| D3D11 | Binding arrays and constant buffers | Constant/parameter-block binding arrays, keyed by register offsets |
| CPU | Serialized parameter graphs | Serialized constant/parameter-block subgraphs |
| CUDA | Serialized host/device parameter graphs | Persistent device parameter-block storage |

D3D12 allocations use the existing device descriptor heaps with persistent ownership. Vulkan uses the existing device descriptor allocator, with allocation and freeing protected by a mutex. Command-buffer transient allocators continue to serve mutable data.

D3D12 and Vulkan process resource uses even when binding-data pointers match the previous command. A cache hit can suppress redundant native bindings without suppressing required synchronization.

Vulkan parameter blocks containing push-constant ranges conservatively use normal descriptor assembly because their push-constant placement depends on the enclosing root. Their finalized uniform buffers are still reused; fully finalized roots cache the complete binding data. CUDA retains the existing per-launch update of module-global parameters.

Persistent storage trades memory and first-use cost for lower repeated CPU cost. Uniform storage currently uses individual buffers per object/layout, and preparation records remain until the object and recorded commands release them. There is no cache budget or eviction policy. Uniform-buffer suballocation, automatic revision caching for mutable objects, and dynamic-offset bindings are possible follow-ups.

## Benchmark

Build and run an optimized configuration:

```powershell
cmake --build build --config Release
./build/Release/slang-rhi-tests.exe '-tc=benchmark-finalized-shader-object*'
```

The benchmark compares five modes: mutable bindings, a finalized block in a mutable root, a finalized root, a changing mutable root, and a changing mutable root with a finalized block. The last two update one root uniform every dispatch and use their own baseline.

Each mode runs with 1 and 1,000 dispatches per command buffer. It discards one warmup, rotates mode order, and reports the median of seven samples. Objects persist across samples and command buffers. The shader reads four structured-buffer bindings and uniforms from a parameter block, root, and entry point, and accumulates into a UAV. Every sample checks the result after GPU completion.

Output lines beginning with `binding-benchmark,` report nanoseconds per dispatch for encoding, finishing, submission, and their total, plus speedups against the corresponding mutable baseline. The total is measured directly; separately reported component medians need not sum to it. GPU waiting and readback are outside timing. Object creation, finalization, pipeline creation/binding, and initial encoder setup are also outside timing. The benchmark measures warm CPU costs, not GPU execution speed or first-use amortization. Single-dispatch figures include command-buffer overhead and can be dominated by timer granularity or transient-pool setup. Use Release: Debug builds always enable validation.

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

## Correctness coverage

`tests/test-finalized-shader-object.cpp` covers recursive immutability, changing roots and entry points with frozen children, shared blocks at different binding positions, changing resource contents, command-buffer recycling, releasing shader objects before submission, alternating specialized pipelines, and concurrent first use on CPU, D3D12, Vulkan, and WebGPU. Benchmark samples also verify results.

The implementation was built and exercised on CPU, CUDA, D3D11, D3D12, Vulkan, and WebGPU. Metal changes require compilation and validation on macOS.

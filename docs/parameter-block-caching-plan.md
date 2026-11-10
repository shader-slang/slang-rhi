# Finalized parameter-block caching plan

Status: proposed implementation plan. This document describes intended behavior,
not an implemented feature. The prerequisite Vulkan synthetic-resource policy is decided: extra bindings
must occupy separately owned sets, implemented in a separate change before caching.

## Objective

Reuse the prepared backend representation of a finalized shader object bound as
`ParameterBlock<T>` across dispatches and programs within one command buffer.
The primary workload is a large scene object shared by multiple compute and
ray-tracing shaders, with mutable roots supplying per-dispatch parameters.

For compatible block layouts, preparing another root must not recreate the
scene's descriptor tables/sets, repack its data, upload its constants, or repeat
its recursive resource-retention traversal.

```text
finalized scene object
    -> cached prepared scene, per compatible layout
        -> mutable compute root A
        -> mutable compute root B
        -> mutable ray-tracing root
```

Native binding commands, synchronization, and residency declarations remain
necessary where required by each use. Reusing descriptor storage does not imply
that bindings remain valid across pipeline or native encoder changes.

## Scope and decisions

- Keep the existing `finalize()` API; no shader-object encoder or new public
  immutable-object type is required.
- Cache finalized objects at parameter-block boundaries, including their frozen
  descendants. A root does not need to be finalized to benefit.
- Share entries throughout one command-buffer recording, across passes and
  compatible programs. Resetting/recycling the command buffer invalidates its
  entries. Separate command buffers prepare separate entries initially.
- Support all backends where the relevant parameter-block functionality exists.
  Use capability checks for tests, including ray tracing.
- Keep mutable objects on the existing transient preparation path. Avoid adding
  per-dispatch hashing of object contents, shared cache locks, or persistent
  allocations to that path.
- Treat equivalent block layouts in different programs as shareable. Different
  effective layouts may require separate cached representations of one object.
- Preserve independently reusable parameter-block binding boundaries. Do not
  force changes to ordinary shader layouts merely to accommodate caching.

Whole-root caching, reuse across command buffers, mutable-object version caching,
and general caching of arbitrary inline values or constant-buffer children are
outside this first implementation. These may be added independently later.

## Finalization contract

Finalization freezes ordinary data, resource bindings, specialization arguments,
and the transitive child graph. Finalizing a root also freezes its entry-point
objects. Freezing a shared child affects all references to that child.

Referenced resource contents remain mutable: buffers can be written, textures
updated, and acceleration structures used according to their existing API
contracts. Finalization does not snapshot resource contents or remove the need
for synchronization. Replacing a bound resource requires another mutable object.

Backend preparation is lazy on first use with an effective layout. `finalize()`
does not promise to allocate descriptors or compile all consuming programs.

Before enabling cache hits, incorporate or reconcile the fixes in
[PR #950](https://github.com/shader-slang/slang-rhi/pull/950): record finalization,
freeze entry points, and guard specialization setters. Audit all mutation paths
and test enforcement with and without the debug layer. Preserve the existing
`reserveData()` contract: its returned pointer is populated immediately and must
not be retained for later mutation.

## Preparation and attachment

Split parameter-block binding into two internal operations:

```text
prepareParameterBlock(object, effectiveBlockLayout, recordingCache)
    mutable: use the existing preparation path
    finalized: find or build a reusable prepared block

attachParameterBlock(preparedBlock, occurrencePlacement, rootBindingData)
    connect the prepared representation to this root's binding locations
```

The exact internal types are implementation details. Backend payloads remain
backend-specific, with common rules for eligibility, identity, and lifetime.

A prepared block contains or references:

- Uploaded ordinary data and its backing allocation.
- Native descriptor sets/tables, argument buffers, or packed parameter data.
- Prepared nested parameter blocks and their relative placement information.
- Resource ownership and reusable resource-use information.

Program-dependent absolute set numbers, root-parameter indices, buffer slots,
and pointer destinations belong to attachment. A scene at Vulkan set 1 in one
program and set 3 in another can share its descriptor storage if the respective
set layouts match. Nested placement must also be represented relative to the
block, or through an occurrence-specific mapping when a simple offset is
insufficient.

On a cache hit, skip preparation of the entire frozen subtree. On a miss, build
the subtree and publish the entry only after successful preparation. Retain the
existing error propagation to command-encoder `finish()`.

Refactor builders that currently append directly to root binding arrays so they
can attach reusable block records. Avoid copying a large scene's resource-use
list into every root; roots should be able to reference reusable lists or spans.

## Cache identity and layout compatibility

The conceptual key is:

```text
(retained shader-object identity, effective block-layout identity)
```

The command buffer already establishes the device and allocation scope. Object
identity is sufficient for contents because the object and its descendants are
frozen. Retaining the object prevents address reuse during the cache lifetime.

Layout identity must capture both data packing and the native representation:
ordinary-data offsets and sizes, specialization, resource kinds and counts,
descriptor ordering and layout requirements, and relevant nested layouts. It
must distinguish any backend-specific properties that affect compatibility.

Absolute placement in a consuming root is excluded when it does not affect that
representation. Program or pipeline identity alone is too restrictive; a Slang
type name alone is insufficient. A program-local layout pointer also must not
prevent sharing between structurally equivalent layouts from different programs.

Determine whether existing layout caching supplies this identity; otherwise
intern compatible layout descriptions when layouts are constructed. Do not walk
reflection or hash large layout descriptions on every cache lookup. Verify both
equivalent-layout sharing and incompatible-layout separation explicitly.

Freeze-dependent specialization information should be reusable for finalized
children. Root specialization and root binding construction may still occur per
dispatch; eliminating all such work belongs to the separate root optimization.

## Ownership and synchronization

Keep prepared payloads and allocations within the existing command-buffer
ownership model. Descriptor pools, descriptor arenas, constant storage, and CPU
arenas must remain valid through execution and be reset consistently with the
cache. Retain objects and resources with the appropriate internal ownership
edges described in [object-lifetime.md](object-lifetime.md); avoid device cycles.

Resource retention is established once per cached representation in a command
buffer, including backend-generated buffers and resources needed by indirect
bindings. Reusing a block must not depend on application handles remaining alive.

Separate resource-use processing from descriptor-binding changes:

- Process resource-state requirements at every consuming draw or dispatch,
  including when the prepared block or root binding pointer is unchanged.
- Preserve required UAV dependencies and transitions after intervening copies,
  indirect-argument uses, explicit state changes, or acceleration-structure work.
- Derive stage-dependent synchronization from the consuming context where needed;
  a compute use must not constrain a later ray-tracing use incorrectly.
- Apply residency declarations at the scope required by the native API, including
  new Metal encoders, even when prepared resource lists are reused.
- Continue rebuilding mutable root bindings and tracking their resources.

Per-recording caches avoid introducing shared cache synchronization. Do not store
a single command-buffer-local prepared pointer on a shader object that may be
used by another command buffer. Any new object-level memoization must respect
the existing threading contract.

## Backend work

| Backend | Prepared block | Attachment and implementation work |
|---------|----------------|------------------------------------|
| Vulkan | Descriptor sets, ordinary-data storage, nested blocks, resource uses | Add lookup around `bindAsParameterBlock`; place reused handles into the root's set array; reuse independently owned sets using their occurrence placements; preserve per-use barriers. |
| D3D12 | Resource/sampler tables, constant allocations, relative root-binding records, resource uses | Separate table creation from writing absolute root-parameter indices; keep existing GPU heaps and transient arenas; process states independently of rebinding. |
| Metal | Argument-buffer hierarchy, ordinary data, retained resources and residency lists | Cache argument-buffer construction; attach the buffer at the current slot; preserve encoder-specific residency declarations. |
| WebGPU | Bind groups for the effective layout, uniform storage, nested groups | Separate block group creation from root-wide group assembly; retain upload ordering and group ownership. |
| D3D11 | Constant allocations and relative resource-binding arrays | Prepare reusable block records and attach them at reflected root offsets; native slot binding remains necessary. |
| CUDA | Packed global-memory block data, child pointers, host upload storage | Reuse recursive block packing and allocations; write the block pointer into each root; preserve upload ordering and existing launch-time operations. |
| CPU | Packed block data and child pointers | Reuse recursive packing; write the prepared block pointer into each root. |

WebGPU currently derives native layouts from compiled resource usage and stage
visibility. Preserve that behavior initially and cache separate effective-layout
variants where necessary. Do not force a universal superset layout without
evaluating its binding limits, resource requirements, and performance effects.

Vulkan descriptor reuse requires matching set layouts, but does not require the
entire consuming pipeline layouts to be identical. Bind the reused sets at the
current placement and bind point; do not conflate descriptor reuse with the
rules for preserving previously bound state across pipeline changes.

## Prerequisite: independently owned synthetic sets

The compiler investigation confirmed that the Slang version used by the RHI
already places automatic Vulkan coverage resources in a separate descriptor set.
Mixed sets were an explicit-placement capability supported by the optional RHI
extension, rather than a requirement of normal coverage allocation.

Implement the following in a separate prerequisite change:

- Reject synthetic resources in sets occupied by ordinary reflected resources,
  including parameter blocks, even if their binding numbers are unused.
- Allow several synthetic resources in a synthetic-only set at distinct bindings.
- Preserve explicit set placement and gaps while keeping parameter-block native
  layouts and descriptor allocation independent of the root's synthetic sets.
- Remove descriptor merging; retain the placement information needed for sparse
  set numbers and nested/entry-point objects.
- Document that affected explicit overrides need a dedicated set and shader
  recompilation. Preserve automatic coverage and CUDA support.

This is the selected alternative to reverting the synthetic-resource feature.
Parameter-block caching must build on this independent ownership contract rather
than introduce partial-set caching or a mixed-set fallback.

## Implementation sequence

1. Land the separate synthetic-set ownership prerequisite described above.
2. Establish and test the finalization contract, reconciling PR #950.
3. Establish canonical effective block-layout identity and command-buffer cache
   ownership. Define the preparation/attachment separation and resource-use
   composition needed by backend builders.
4. Implement Vulkan and D3D12 parameter-block reuse, including nested blocks,
   different root placements, compute/ray-tracing sharing, and per-use state
   handling. Keep the structure suitable for the remaining backends.
5. Implement Metal, WebGPU, D3D11, CUDA, and CPU equivalents where supported.
   Verify native encoder changes and backend-specific upload/retention behavior.
6. Run the correctness and performance matrix below, then document supported
   behavior and any explicitly agreed limitations. Whole-root and persistent
   caching remain separate follow-ups.

## Validation and acceptance criteria

Add focused tests using the device capability system. Test correctness and
observe actual preparation work through internal test instrumentation; correct
output alone cannot distinguish reuse from rebuilding.

- One finalized scene used repeatedly by mutable roots with changing constants
  and output bindings.
- The same scene used by multiple programs with equivalent block layouts,
  different root placements, and both compute and ray tracing where supported.
- The same object appearing more than once in a root at different placements.
- Nested parameter blocks, arrays of blocks, and blocks with no own bindings.
- Different effective layouts producing distinct cache entries; equivalent
  layouts from different programs sharing an entry.
- Mutation rejection through existing child handles and specialization setters,
  including debug-layer-disabled execution.
- Mutable child changes continuing to affect later dispatches when the block is
  not finalized.
- Intervening copies, indirect uses, UAV dependencies, pass changes, and native
  encoder changes preserving synchronization and binding correctness.
- Application references released after recording; resources remain alive until
  execution completes. Command-buffer reset/recycling never reuses stale data.
- Separate command buffers using the same finalized object without sharing
  transient allocations accidentally.

For an independently owned block with a compatible effective layout, descriptor
construction, ordinary-data packing/upload preparation, and recursive resource
retention occur once per command buffer. Later attachments may perform small
binding-record work and required resource-use processing, but do not rebuild the
scene. Actual upload timing remains backend-specific.

Extend [test-benchmark-command.cpp](../tests/test-benchmark-command.cpp) to compare
mutable blocks with finalized blocks under repeated and alternating programs.
Include a large scene, many distinct blocks, and single-use blocks to expose
cache overhead. Measure root recording, `finish()`, allocations, upload bytes,
and retained memory separately; account for backends that execute work at submit.
Warm shader compilation and run performance measurements in Release without
validation layers. Use validation-enabled runs for correctness.

The performance goal is a substantial reduction in repeated scene preparation
without a material regression to mutable workloads. Report measured results
rather than promising a specific speedup. Exercise available local backends and
record which backend checks require other platforms or CI.

## References

- [Issue #955: finalized root binding reuse](https://github.com/shader-slang/slang-rhi/issues/955)
- [PR #950: finalization fixes](https://github.com/shader-slang/slang-rhi/pull/950)
- [Shared shader objects](../src/shader-object.cpp)
- [Command binding preparation](../src/command-buffer.cpp)
- [Vulkan parameter-block builder](../src/vulkan/vk-shader-object.cpp)
- [Vulkan descriptor-set placement](../src/vulkan/vk-descriptor-set-placement.cpp)
- [D3D12 parameter-block builder](../src/d3d12/d3d12-shader-object.cpp)
- [Synthetic resource bindings](synthetic-bindings.md)

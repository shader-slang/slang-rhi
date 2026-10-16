# Cluster acceleration structure example plan

The implementation is now consolidated in `examples/cluster-displacement/example-cluster-displacement.cpp`,
with GPU code in `cluster-displacement.slang`. Usage and design notes are source comments.
The example-specific regression tests were removed after the validation recorded below;
test commands and test descriptions in this plan describe that historical validation.

Add an interactive ray tracing example that generates a procedurally displaced
quad and rebuilds its cluster acceleration structures entirely on the GPU. Ship
uniform dynamic subdivision first, then extend the example with spatially
adaptive subdivision using a bounded quadtree and watertight patch transitions.

Status: stages 1, 2, and 3 implemented and validated on D3D12, Vulkan, and CUDA/OptiX
(RTX 5090).
The adaptive subdivision and uniform-reference milestones are complete.

## Goals and scope

The example should make the complete geometry and acceleration structure build
chain easy to understand: generated triangles become cluster acceleration
structures (CLAS), their handles become a bottom level acceleration structure
(BLAS), and a top level acceleration structure (TLAS) makes the surface available
to ray tracing.

All per-frame subdivision decisions, vertex generation, build arguments, counts,
and handle propagation stay on the GPU. The CPU creates resources and pipelines,
supplies camera and animation parameters, and records a fixed sequence of GPU
commands. No geometry, count, or handle readback is required to advance a frame.
Normal presentation and resource-lifetime synchronization remain necessary.

The initial supported rendering backends are D3D12, Vulkan, and CUDA/OptiX. Require
`Feature::Surface`, `Feature::AccelerationStructure`,
`Feature::ClusterAccelerationStructure`, and `Feature::RayTracing`, and report
missing features through the existing example framework. CUDA is an acceptance
requirement for every stage, including the windowed example. Use the existing
CUDA presentation, blitter, and compute text-overlay paths. Devices without the
required OptiX cluster support must report missing features cleanly.

The first shippable milestone is stage 2 below. Stage 3 completes the spatially
adaptive design. Cluster templates, persistent geometry caches, streaming,
AS memory compaction, async compute, and a general terrain system are outside the initial
scope. No public RHI API additions are planned; any implementation gap discovered
while validating the GPU build chain should be addressed narrowly.

## Scene and interaction

Use a single finite quad in the XZ plane, displaced along Y by a deterministic
function of world position and time. Start with a small sum of sine waves with
known amplitude and frequency. This provides bounded displacement, reproducible
images, and a useful surface for inspecting silhouettes and shadows.

Render primary rays with directional lighting and a shadow ray. Use the existing
camera, window, blitter, and text overlay infrastructure. Progressive path tracing
and temporal accumulation are unnecessary for the initial example.

Provide controls to pause displacement, freeze LOD selection while continuing to
move the camera, adjust displacement amplitude and subdivision quality, and
switch between shaded, cluster-color, subdivision-level, and triangle-edge views.
Use geometric normals initially so shading reveals the actual tessellation.

Keep the whole surface in the acceleration structure. Camera-frustum culling
would otherwise remove geometry that may still cast shadows. LOD selection may
be camera-dependent, but every selection must cover the complete quad.

## Patch representation and resource bounds

Each active patch uses a regular grid of 8 by 8 cells, with 128 triangles and
81 vertices before transition stitching. One patch produces one CLAS. These
counts fit within the current limits of 256 triangles and 256 vertices per
cluster in [slang-rhi-device.h](../include/slang-rhi-device.h).

Use local indices and shared index patterns. Compute writes each patch's displaced
vertices, metadata, and `TriangleClusterArgs`. Assign a dense cluster ID for the
current frame and keep the patch coordinates and level in a metadata buffer
indexed by that ID. Derive debug colors from patch coordinates and level so
compaction order does not change their appearance.

Use a bounded implicit quadtree for stage 3. Identify a node by its level and
integer coordinates or Morton code; derive parent and child relationships
without pointers or dynamic allocation. The surface hierarchy is separate from
the acceleration structures used for ray traversal.

Start with a maximum depth of six, where the root is depth zero. This permits
4,096 leaves, 331,776 patch vertices, and 524,288 regular-grid triangles. A complete
tree contains 5,461 nodes. These are initial capacity choices, not performance
targets. Confirm that transition patterns fit the chosen per-cluster and total
capacity bounds before implementing stage 3.

At initialization, call `getClusterOperationSizes` for the configured maximum
counts and allocate the CLAS and BLAS result and scratch buffers accordingly.
Also allocate patch metadata, vertices, index patterns, build arguments, counters,
handle arrays, a native TLAS instance descriptor, and the TLAS resources. Respect
each operation's alignment requirements and account for every concurrently live
resource set when reporting memory use.

Preallocate for the full subdivision bound initially. A future smaller work
budget must retain a valid coarser covering of the surface when a split cannot
be accepted; truncating the compacted leaf list would create holes.

## GPU frame sequence

Record the following sequence on one queue. The CPU knows the maximum work size
and the fixed pass sequence, but does not need the current leaf count.

1. Reset per-frame counters and compute subdivision decisions. Stage 1 uses a
   fixed patch layout. Stage 2 chooses one level for the whole surface. Stage 3
   selects and balances an adaptive tree.
2. Compact active leaves into patch metadata and write the active cluster count.
   Generate indirect compute dispatch arguments where useful; bounded dispatches
   with GPU count checks are also acceptable for this small example.
3. Generate displaced vertices and `TriangleClusterArgs`. Select the regular or
   transition index pattern for each patch. Write one `ClusterArgs` record with
   the active CLAS count and the address of the CLAS handle array.
4. Execute `CLASFromTriangles` in `ImplicitDestinations` mode. The argument count
   comes from the GPU buffer; output handles and CLAS data remain on the GPU.
5. Execute `BLASFromCLAS` for one BLAS, consuming the CLAS handle array and the
   GPU-written `ClusterArgs` record. Its operation argument count is always one.
6. Run a compute pass that writes the generated BLAS handle into the native TLAS
   instance descriptor. Initialize the static identity transform and instance
   fields once, using the backend's descriptor layout.
   For CUDA, use the native `OptixInstance` layout and patch `traversableHandle`
   (byte 64), rather than the D3D12/Vulkan address field (byte 56). Derive offsets
   and strides from the public native descriptor declarations and conversion
   helpers. CUDA compute, cluster builds, TLAS builds, and tracing must remain
   ordered on the same stream, without a CPU handle/count handoff.
7. Rebuild the one-instance TLAS.
8. Trace the image with a pipeline configured
   with `RayTracingPipelineFlags::EnableClusters`.
9. Draw diagnostic views and the overlay, then present.

Submit steps 1-7 before recording the shader bindings for
tracing in a second command buffer on the same queue. The CUDA backend obtains
the TLAS traversable handle during build submission, while shader binding
recording captures its current value. Recording both in one command buffer can
therefore bind a null handle on the first frame. Queue ordering supplies the GPU
dependency between submissions; no intermediate host wait or GPU readback is
needed. This does not change the GPU-only CLAS-to-BLAS-to-instance handle chain.

Rebuild all active CLAS and the BLAS every frame initially. Animated displacement
changes their geometry, and changing subdivision can change topology. Do not
assume ordinary AS refit semantics apply to cluster operations.

Explicitly verify dependencies from compute writes to CLAS builds, CLAS outputs
to BLAS builds, BLAS handles to the instance-writing compute pass, instance writes
to the TLAS build, and TLAS writes to tracing. Buffers reached through device
addresses need correct states, visibility, and lifetime even when the encoder
cannot infer them from shader bindings. Verify scratch reuse and same-state write
hazards as well as resource-state transitions.

Keep geometry, metadata, and acceleration structures consistent until all rays
using them have completed. Start with serial execution on one queue and ensure
the next frame's writes follow the previous frame's reads. If the framework needs
multiple resource sets, retire and reuse them through its fence conventions.

## Subdivision and watertight transitions

For stage 2, compute one level from projected surface size and a quality setting.
Generate a uniform covering of `4^level` patches directly, without a quadtree
traversal. Preserve the selected level on the GPU while LOD is frozen. Add
hysteresis around level thresholds to avoid rapid oscillation.

For stage 3, recompute the active hierarchy using a bounded sequence of level
passes. Begin with projected patch size and conservative displacement bounds as
the refinement criterion. A curvature or displacement-error criterion can follow
after the geometry and build chain are validated. Preserve GPU selection state
as needed for hysteresis and freezing; a persistent split/merge allocator is not
required.

Balance neighboring leaves so their levels differ by at most one. Complete
balancing before final compaction and geometry generation. Choose a bounded
propagation scheme with a justified pass limit and verify the final neighbor
constraint; an arbitrary small number of balancing iterations is insufficient.

Use transition index patterns on fine patches next to coarse neighbors. Cover
all edge masks and corner combinations, keeping the mesh connected without
unmatched boundary vertices. Evaluate shared vertices from canonical integer
surface coordinates using the same displacement expression and time value.
Matching coordinates alone does not solve a fine edge whose extra displaced
vertices deviate from the coarse edge's linear segments; transition topology must
remove that mismatch.

Skirts are not the intended final solution because their extra geometry can
affect shadows and other rays. Smooth geometric transitions between LOD levels
are a later refinement; the first adaptive version may exhibit LOD popping but
must remain watertight.

## Implementation stages and acceptance criteria

### Stage 1 Animated surface with fixed patches

- [x] Add `examples/cluster-displacement/example-cluster-displacement.cpp` and
  `cluster-displacement.slang`, with an `example-cluster-displacement` CMake target.
- [x] Reuse the example framework and gate execution on the required features.
- [x] Implement fixed patches, GPU displacement, and the complete GPU build chain.
- [x] Add shaded rendering, shadows, cluster colors, and animation pause.
- [x] Verify the GPU-written TLAS instance descriptor on all three target backends.

Accept when an animated surface renders correctly on supported D3D12, Vulkan, and CUDA
devices, unsupported devices report missing features cleanly, and no per-frame
geometry or AS handle readback is needed. Validation must report no resource or
synchronization errors attributable to the example.

The stage 1 milestone used 64 fixed patches (8 by 8), with 5,184 vertices and
8,192 triangles. At that milestone, a shared helper recorded the same GPU build chain
for the example and its regression test. Each operation has separate scratch
storage. One resource set is retired with a queue wait before the next frame;
there is no geometry, count, or handle readback in the example. Native instance
transforms and flags are initialized through the existing conversion utility;
compute replaces only the backend-specific BLAS address or OptiX traversable
handle field. Build and trace submissions use the ordering described above,
including on the first frame.

Stage 1 controls: left drag to look, W/A/S/D to move, Q/E down/up, Shift for faster
movement, Space to pause, C for cluster colors, brackets to change amplitude,
R to reset the camera, and F1 to toggle the overlay.

Stage 1 validation on 2026-10-16:

- Debug builds of `example-cluster-displacement` and `slang-rhi-tests` passed.
- `cluster-displacement-gpu-build-chain` passed on D3D12, Vulkan, and CUDA/OptiX
  with an NVIDIA GeForce RTX 5090 (5,875 assertions in the combined run). It checks
  the GPU-written counts and native instance handle, preservation of every other
  native instance byte, first-frame tracing, flat and animated hit distances,
  cluster/triangle IDs, outside-surface misses, and reused allocations while
  changing the patch count through 1, 64, 4, 1, and 64. Readback is assertions only.
- All three windowed backends rendered shaded and cluster-color views. Pause produced
  identical successive captures; resuming changed the image. Amplitude controls,
  resizing, minimize/restore, and sustained frame reuse were exercised.
- D3D11 skipped cleanly with the missing acceleration-structure,
  cluster-acceleration-structure, and ray-tracing features listed.
- The CUDA regression skipped cleanly with `-optix-version=80100` (OptiX 8.1),
  reporting that cluster ray tracing is not supported.
- Native core validation, Vulkan synchronization validation, and CUDA/OptiX
  ray-tracing validation reported no
  resource/synchronization errors attributable to the example. The installed
  Vulkan SDK 1.4.335.0 validation layer predates three device extensions enabled
  by the existing backend. Even the unchanged cluster sizing test fails device
  creation with `VUID-VkDeviceCreateInfo-pNext-pNext`. Vulkan validation runs used
  the process-local `VK_LAYER_MESSAGE_ID_FILTER=VUID-VkDeviceCreateInfo-pNext-pNext`
  and `VK_LAYER_VALIDATE_SYNC=true`; unknown-extension warnings remained visible.
  No repository validation settings or backend code were changed for this issue.

Run the example with `build/Debug/example-cluster-displacement --device d3d12`
or `--device vulkan` or `--device cuda`. Run the regression with
`build/Debug/slang-rhi-tests -tc="cluster-displacement*" -select-devices=d3d12,vulkan,cuda`
(quote the complete `-select-devices=...` argument in PowerShell).
Use the process-local Vulkan settings above only when testing with that older
validation layer. All builds and test runs were outside the sandbox.

### Stage 2 Uniform dynamic subdivision

- [x] Select a uniform subdivision level on the GPU and generate its patch list.
- [x] Consume GPU-generated counts throughout geometry generation and CLAS/BLAS
  construction, including transitions from many patches to few patches.
- [x] Add quality controls, LOD freeze, level colors, and triangle-edge display.
- [x] Document controls, resource bounds, and the build sequence alongside the
  example, following repository conventions.

Accept when camera movement and quality changes alter geometry density without
CPU readback, the full surface remains covered, no stale handles are consumed
after decreasing the count, and minimum and maximum levels render correctly.
This is the first shippable milestone.

Stage 2 selects levels 0-6 in a compute pass using projected surface size, a
quality multiplier, and 15% hysteresis around refinement/coarsening thresholds.
The selected level and initialization flag persist on the GPU. A fixed dispatch
of 4,096 groups generates the active prefix of patches using GPU count checks;
no indirect compute count readback is needed on CUDA. CLAS builds and the BLAS
handle list consume the same GPU-generated count. Resources are preallocated for
4,096 patches, 331,776 vertices, and 524,288 triangles, using one resource set.

Comments in the [example source](../examples/cluster-displacement/example-cluster-displacement.cpp) document the
controls, selection rule, resource bounds, memory reporting, and build sequence.
F freezes LOD independently of Space (animation pause); minus/equals changes
quality from 0.125x to 8x. Keys 1-4 select shaded, cluster, level, and triangle-edge
views, and C cycles them. The overlay reports capacity and control settings
without reading the selected level or counts back to the CPU.

Stage 2 validation on 2026-10-16:

- Debug builds and the GPU regression passed on D3D12, Vulkan, and CUDA/OptiX
  using the RTX 5090: 25,072 assertions across all three backends.
- The regression exercises 29 frames per backend using camera, projection, and
  quality inputs. It covers every level, repeated 4,096-to-one transitions,
  both hysteresis bands, near-camera capacity clamping, viewport changes,
  frozen first-frame initialization, and frozen camera/quality/viewport changes.
  Assertions verify dense complete patch coverage, CLAS and BLAS counts, native
  instance preservation, and flat/animated ray hits after each rebuild.
- All three windowed backends rendered the four views. Quality changes and
  camera movement reached both level 0 and level 6. Frozen LOD survived camera
  reset, quality changes, resize, and minimize/restore; unfreezing resumed
  selection. Animation pause/resume remained independent.
- Native validation, Vulkan synchronization validation, and OptiX validation
  reported no resource/synchronization errors attributable to the example.
  Vulkan used the same process-local older-layer workaround documented above.
  All builds and tests ran outside the sandbox.

### Stage 3 Spatially adaptive subdivision

- [x] Add implicit quadtree selection, neighbor balancing, and leaf compaction.
- [x] Implement and validate every transition edge mask and corner combination.
- [x] Add adaptive selection hysteresis and preserve the existing freeze control.
- [x] Retain uniform subdivision as a reference and diagnostic mode.

Accept when nearby regions refine independently, every selected leaf covering is
complete and nonoverlapping, adjacent levels satisfy the 2:1 constraint, and
mixed-level boundaries remain watertight under primary and shadow rays. Rapid
camera movement and repeated split/coarsen cycles must remain within capacity.

Stage 3 uses six top-down selection passes and five descending balancing passes.
For each split node at level L, balancing marks its own parent and the parents
of its four same-level face neighbors as split. A pass writes only level L-1;
the next descending pass propagates those requirements. Thus every split's
children border leaves at least as fine as their parent, establishing the 2:1
constraint with a depth-derived bound rather than a guessed iteration count.
GPU compaction appends exactly one metadata record per leaf and computes its
coarse-neighbor edge mask before geometry generation.

All 16 transition masks use shared index patterns. Each stitched edge removes
its four odd boundary vertices from the topology. Fans in 2-by-2 cell blocks
handle corners without degenerate triangles or internal T-junctions. Patterns
have 112-128 triangles, within the original allocation bounds. Displacement uses
canonical integer coordinates on the finest 512-by-512 cell grid, ensuring
bit-identical shared positions. Adaptive selection retains 15% hysteresis and
freeze state; T switches to the independently retained uniform reference.
One resource set now requests about 27.19 MiB on the validated RTX 5090.

Stage 3 validation on 2026-10-16:

- Debug builds of the example and test executable passed.
- The uniform, adaptive, and topology regressions passed on D3D12, Vulkan, and
  CUDA/OptiX with the RTX 5090: 7 tests and 7,871,076 assertions passed.
  Adaptive tests run 18 frames per backend, including
  near-corner teleports, multiple simultaneous levels, projection/quality changes,
  first-frame freeze, root split/merge hysteresis, frozen animation changes,
  switching through uniform mode, and repeated full-capacity/coarsest rebuilds.
- Readback checks verify complete nonoverlapping coverage, every neighbor's 2:1
  constraint, masks and triangle build arguments, identical displaced boundary
  positions, and exactly two incident patches per internal boundary segment.
- All 16 masks pass topology checks for winding, area, corner connectivity,
  manifold interior edges, and the exact expected boundary segments. GPU rays
  check exact seam crossings and surface-relative grazing primary/first-hit
  shadow rays just inside incident triangles. Flat cases match analytic plane
  distances. Grazing probes avoid the ambiguous case of merely touching a crease.
- All three windowed backends rendered all four views, including adaptive level
  colors and transition edges. Camera movement, animation, adaptive/uniform
  switching, freeze, quality changes, resizing, and
  minimize/restore were exercised with the compute overlay and presentation path.
- Native validation, Vulkan synchronization validation, and OptiX validation
  reported no example resource/synchronization errors. Vulkan used the same
  process-local older-layer workaround described above. All builds and tests
  ran outside the sandbox.

## Validation strategy

Extend the cluster tests with a focused regression that writes geometry, counts,
and build arguments on the GPU, builds CLAS and BLAS, patches the TLAS instance
on the GPU, and traces known rays. Readback is permitted for test assertions;
it must not become an input to the build chain. Exercise both increasing and
decreasing cluster counts and run on supported D3D12, Vulkan, and CUDA devices.
On CUDA, also verify that patching the traversable handle preserves the rest of
the native instance descriptor and that windowed rendering works with the
compute overlay and CUDA presentation path.

For stage 3, test leaf coverage, neighbor balance, and transition topology for
all masks, including corners and the outer surface boundary. Validate shared-edge
positions and use grazing primary and shadow rays to expose cracks. Compare a
flat displacement case against a simple known plane and use fixed camera/time
settings for reproducible checks.

Manually exercise animation pause, LOD freeze, quality limits, large displacement,
camera movement across thresholds, resize/minimize, and resource reuse over many
frames. GPU diagnostics may show active patch and triangle counts directly. If
CPU overlay statistics are added, use optional delayed readback that does not
control rendering.

Run all CMake configuration/build commands and all tests outside the sandbox,
as required by the repository instructions. Record which backend/device pairs
were actually validated and distinguish unsupported hardware from test failures.
Performance measurements are diagnostic; no speedup target is assumed.

## Later extensions

After the initial milestones, consider `TemplatesFromTriangles` and
`CLASFromTemplates` for repeated topology. The regular grid and transition
patterns provide a small set of candidate templates. Validate template bounds
and backend behavior before comparing them with direct triangle builds.

Other optional extensions include GPU timestamps for selection, geometry, CLAS,
BLAS, TLAS, and tracing; improved displacement-error estimates; LOD morphing;
and caching clusters when displacement is paused. These should preserve the
simple direct-build path as an instructional reference.

## Repository references

- [Cluster API](../include/slang-rhi.h): operation types, modes, sizing, and buffers.
- [Shared device declarations](../include/slang-rhi-device.h): GPU build arguments,
  limits, and native instance descriptor layouts.
- [Cluster tests](../tests/test-ray-tracing-clusters.cpp) and
  [shaders](../tests/test-ray-tracing-clusters.slang): build and tracing setup.
  Existing helper readbacks should not be copied into the example's frame loop.
- [Path tracer](../examples/path-tracer/example-path-tracer.cpp) and
  [example framework](../examples/base/example.h): window, camera, and rendering
  integration.
- [CMake configuration](../CMakeLists.txt): example target registration.

# Example ideas

This document collects proposed examples and extensions for future implementation.
The goal is a small collection that demonstrates distinct slang-rhi capabilities,
with shared interaction across side-by-side backend windows. These are candidates,
not a commitment to implement every idea.

## Existing coverage

- Surface and triangle examples introduce presentation and basic rasterization.
- ShaderToy demonstrates compute-to-texture rendering and interactive shaders.
- The [raster logo example](../examples/raster/README.md) demonstrates indexed
  geometry, depth testing, metallic/roughness shading, PCF shadow mapping, HDR
  rendering, compute bloom, and tone mapping. Its logo mesh and material data
  are independent of the renderer for later reuse in the path tracer.
  It also includes crease-aware smooth normals, a filtered procedural ground
  pattern, and a reusable bitmap-text HUD with application-loop FPS reporting.
- The path tracer demonstrates triangle acceleration structures, ray tracing,
  accumulation, and tone mapping. It currently has ray-pipeline and compute
  ray-query paths selected through `USE_RAYTRACING_PIPELINE`.

The largest remaining general-purpose gap is persistent simulation. The raster
logo example supplies a compact introduction to practical multipass rendering.
Advanced ray-tracing examples could additionally
show geometry and acceleration-structure capabilities beyond triangle tracing.

## Priorities and collection size

For general coverage, the proposed order is:

1. Interactive particles, or an interactive compute-to-texture simulation.
2. Material and lighting playground.
3. Extend the existing path tracer with selectable tracing modes and animation.
4. Add GPU-driven rendering to the material playground.
5. Consider a small neural graphics example.

For additional ray-tracing coverage, the proposed order is:

1. Procedural geometry playground.
2. Dynamic cluster acceleration structures.
3. Strands and native spheres, preferably within the procedural playground.
4. Opacity micromaps.
5. Motion blur, preferably within the existing path tracer.

These are two tracks rather than one combined implementation schedule. A compact
general collection would add particles and the material playground. A focused
ray-tracing expansion would add procedural geometry and dynamic clusters, then
extend those examples and the existing path tracer with additional modes.

## General-purpose ideas

### Interactive particles

**Scene:** A large particle cloud follows or repels the mouse, with attractors,
color gradients, and optional trails.

**Capabilities:** Persistent GPU buffers, compute simulation, compute-to-render
resource dependencies, instancing, and blending.

**Initial scope:** Fixed particle count, seeded initialization, a simple force
field, and direct instanced drawing. This provides a clear demonstration of data
produced by compute being consumed by a render pass.

**Possible extensions:** Particle spawning and removal, compaction, GPU-generated
draw counts, and trails. Keep indirect drawing optional: the Metal implementation
does not currently implement indirect draws. CUDA participation would require a
compute visualization path because it does not provide rasterization.

**Why prioritize it:** Strong interaction, little asset preparation, and an obvious
use for mirrored mouse input across backends.

### Alternative: interactive ink or reaction-diffusion

**Scene:** Inject dye or seed patterns with the mouse and watch them evolve.

**Capabilities:** Persistent texture state, ping-pong resources, multiple compute
dispatches, and dependencies between simulation steps.

**Initial scope:** Prefer a small reaction-diffusion simulation for simplicity;
fluid-like ink is another option if a more involved solver is worthwhile.

This fits the existing compute-to-texture presentation approach and is a useful
choice when including CUDA in the same visualization is a priority. Choose this
or particles initially; both fill the persistent-simulation gap.

### Material and lighting playground

**Implemented starting point:** `examples/raster` provides the logo scene, one
shadow map, solid-color materials, HDR tone mapping, and optional bloom. The
broader ideas below remain possible extensions rather than requirements for it.

**Scene:** A small procedural scene with textured objects, a movable light,
shadows, and optional bloom. Share camera and light controls across windows.

**Capabilities:** Indexed geometry, depth testing, textures and samplers,
render-to-texture, multiple render passes, and compute post-processing. Use shader
objects and parameter blocks to illustrate organizing scene and material data.

**Initial scope:** Procedural meshes, a few small textures, one shadow map, and a
simple lighting model. Keep the example readable without an asset-loading engine.

**Possible extensions:** Bloom, material variants, and Slang interface-based
material specialization after checking support on the intended backends.

**Why prioritize it:** Fills the instructional gap between drawing a triangle and
building a path tracer.

### GPU-driven scene mode

**Scene:** Thousands of objects with a debug view showing culling results. A frozen
culling camera could make it easy to inspect which objects survive.

**Capabilities:** Compute culling, compaction, indirect drawing, and optionally
bindless materials.

**Initial scope:** Add frustum culling and GPU-generated draw arguments to the
material playground, reusing its meshes, camera, and shading. Compare against a
direct-draw baseline.

**Possible extensions:** Bindless resource access and more advanced culling.
Check each required capability separately; support for indirect operations and
bindless resource types differs between backends.

Prefer a mode of the raster example over a separate scene and application.

### Tiny neural graphics example

**Scene:** Reconstruct an image or shade a surface with a small pretrained network,
with a reference view and an error visualization.

**Capabilities:** Cooperative vectors or matrices, matrix layout conversion where
needed, and inference integrated into rendering.

**Initial scope:** One small bundled network and fixed weights; no training
pipeline. Compare an ordinary shader implementation with an accelerated path.

**Why later:** Distinctive hardware coverage, but more supporting data and narrower
device support than the general raster and simulation examples. Select the
cooperative operation that fits the workload rather than requiring both APIs.

## Ray-tracing ideas

### Procedural geometry playground

**Scene:** Triangle geometry shares a scene with analytic spheres, capsules, and
one or two bounded signed-distance-field shapes. Users move objects and change
shape parameters while observing shadows and reflections.

**Capabilities:** AABB acceleration structures, custom intersection shaders, hit
attributes and normals, and multiple hit groups within one scene.

**Initial scope:** Start with analytic sphere and capsule intersections plus a
triangle floor. Add a bounding-box view to explain how traversal reaches the
custom intersection shader. Use simple lighting so geometry remains the focus.

**Possible extensions:** One bounded SDF object, editable shape parameters,
acceleration-structure updates, and native primitive comparisons. Keep native
spheres distinct from spheres implemented through custom intersection shaders.

**Why prioritize it:** Clear visual payoff and a practical introduction to geometry
beyond triangles, without requiring large assets or a complicated renderer.

### Dynamic cluster acceleration structures

**Scene:** A densely tessellated sheet or terrain patch ripples around the mouse.
Offer shaded, cluster-ID, and wireframe views.

**Capabilities:** Building cluster-level acceleration structures (CLASes),
assembling a BLAS from CLASes, batched cluster operations, and cluster templates.

**Initial scope:** One procedural surface with fixed topology and animated vertex
positions. Partition it into clusters and trace the result. Then add a template
mode that reuses cluster topology with new vertex positions.

**Comparison:** Use the same geometry for conventional BLAS rebuild/update and
cluster paths. Report acceleration-structure build time, tracing time, and memory
separately. Measure rather than assuming the cluster path is always faster.

**Possible extensions:** Adaptive tessellation, changing cluster sets, GPU-produced
operation arguments, and cluster memory management. Defer these until the basic
example is easy to follow.

The existing cluster tests target D3D12, Vulkan, and CUDA with capability checks.
This is a useful demonstration of a specialized feature exposed through one RHI,
but backend availability alone does not establish hardware support.

### Strands and native spheres

**Scene:** A fiber garden of curved strands, represented by chains of linear swept
spheres, bends around a mouse-controlled force. Spheres at the tips and shadows
make the geometry easy to inspect.

**Capabilities:** Native sphere and linear-swept-sphere build inputs, geometry
updates, and their associated hit information.

**Initial scope:** Procedurally generated strands with simple animated bending.
Avoid a full hair simulation or specialized hair shading model.

**Possible extensions:** Compare native primitives with equivalent custom
intersection implementations where supported, including build and tracing costs.

Prefer a second scene in the procedural playground. Native sphere and swept-sphere
features already have API definitions and tests, but require capability checks.

### Opacity micromaps

**Scene:** A small foliage scene made from leaf cards with cutout textures and
detailed ray-traced shadows.

**Capabilities:** Any-hit alpha testing, opacity micromap construction, and linking
micromaps into acceleration structures.

**Initial scope:** One cutout material and a switch between conventional any-hit
alpha testing and micromaps. Keep the opacity rule consistent between both paths.

**Visualization:** Show tracing time and optionally an any-hit invocation heatmap.
Collect instrumented heatmaps separately from performance measurements. Since the
rendered images should be comparable, the measurements explain the optimization.

This can become another scene in the shared ray-tracing example if doing so keeps
the code understandable. The repository already exposes micromap construction and
linkage; verify the desired path on each target device.

### Extend the existing path tracer

- **Selectable execution model:** Expose the existing ray-pipeline and compute
  ray-query paths as selectable modes, subject to device support. Show the active
  mode so backend comparisons remain clear.
- **Animated instances:** Demonstrate TLAS updates while reusing static BLASes.
- **Deforming geometry:** Demonstrate BLAS updates versus rebuilds, including build
  and tracing costs. This is also useful in the procedural and cluster examples.
- **Motion blur:** Add a rotating fan and deforming ribbon with shutter-duration
  and animation-speed controls. Demonstrate transform motion and vertex motion
  where supported. Sample geometry at ray time within a shutter interval; merely
  accumulating successive animation frames is a different effect.
- **Compaction:** Show acceleration-structure memory before and after compaction,
  with its setup cost, as an optional diagnostic rather than a standalone scene.
- **Shader execution reordering:** Add an optional mode with sufficiently varied
  shading work to make timing comparisons meaningful. Keep the scene and sampling
  workload consistent and do not assume a speedup.

## Shared framework improvements

- Use a shared simulation clock, fixed simulation steps, seeded initialization,
  and synchronized reset, pause, and single-step controls. Mirrored mouse events
  alone do not keep stateful simulations comparable; floating-point differences
  may still cause divergence over time.
- Apply equivalent input coordinates when window sizes differ, and synchronize
  camera, light, and parameter changes as well as mouse events.
- Display the backend, active algorithm, and any unsupported feature or mode.
  Gate advanced paths through device capabilities rather than backend names alone.
- Provide focused debug views: bounds, cluster IDs, culling results, or error maps.
- For performance comparisons, hold resolution, geometry, simulation steps, and
  sample counts constant. Separate setup/build, simulation, and rendering costs
  where supported. Side-by-side execution can introduce GPU contention, so also
  allow isolated runs for meaningful performance measurements.
- Keep procedural assets and scene helpers small. Share camera and presentation
  code without turning the example framework into a general-purpose engine.

## Implementation references

- Existing examples: `examples/triangle`, `examples/shader-toy`, and
  `examples/path-tracer`.
- Ray-tracing setup and procedural build inputs:
  `tests/test-ray-tracing-common.h` and `tests/test-ray-tracing.cpp`.
- Cluster operations: `tests/test-ray-tracing-clusters.cpp` and its Slang shader.
- Native primitives: `tests/test-ray-tracing-sphere.cpp` and
  `tests/test-ray-tracing-lss.cpp`.
- Bindless resources: `tests/test-bindless.cpp`.
- Cooperative operations: `tests/test-cooperative-vector.cpp` and
  `tests/test-cooperative-matrix.cpp`.
- API coverage: [implementation status](../docs/api.md). Check implementation and
  tests as well, since coverage can change.
- [NVIDIA OptiX guide, including cluster templates](https://raytracing-docs.nvidia.com/optix9/guide/optix_guide.250703.LTR.pdf).
- [NVIDIA swept-sphere example](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/samples/18-swept-spheres/).

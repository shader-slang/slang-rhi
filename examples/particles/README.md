# GPU particle energy sculpture

Orange and cyan streams curl around the Slang logo, gather into its shape, then
burst into a cloud of glowing sparks. Velocity stretches particles into streaks;
fine dust and occasional bright leaders give the streams variation in scale.
Mouse movement stirs nearby particles; button-held attraction and repulsion
disturb both the flow and the formed logo.
The example uses procedural sprites and the existing logo mesh, bloom shader,
tone mapper, and bitmap-text renderer; it needs no new assets.

The automatic 16-second cycle flows for three seconds, gathers from seconds
3-7, holds the logo from 7-10, bursts at second 10, then swirls back into the
next cycle. The HUD names the current phase. Traveling curl waves continue
across cycle boundaries so the turbulence and emitters do not jump on wrap.

```sh
cmake --build build --config Debug --target example-particles
./build/Debug/example-particles --device vulkan
```

On Windows the executable has an `.exe` suffix. Omit `--device` for side-by-side
backend windows with mirrored controls. Moving the mouse in any backend window
stirs particles in all of them. Cursor coordinates and hover checks use the
window that supplied the input; switching windows starts a fresh brush stroke.

| Control | Action |
|---|---|
| Move mouse | Stir nearby particles without holding a button |
| Left mouse | Attract particles |
| Right mouse | Repel particles |
| `[` / `]` | Decrease / increase emission, including zero |
| Space | Pause / resume simulation |
| `N` | Advance one simulation step while paused |
| `F` | Burst on the next simulation step, then continue from the burst phase |
| `R` | Empty the cloud and reset the emitter seed/time |
| `B` | Toggle bloom |
| `D` | Toggle indirect / capacity-sized direct drawing |

## GPU data flow

The capacity is 262,144 particles. Each 64-byte particle contains position and
age, velocity and lifetime, color and sprite size, and a persistent logo target,
style seed, and material index. Two structured buffers
alternate between input and output. Each has its own live-count buffer.

Each fixed 1/120-second simulation step records:

1. Clear the destination live counter.
2. Simulate existing particles, discard expired ones, and atomically compact
   survivors into the destination buffer.
3. Append particles up to the remaining capacity. The survivor count stays
   unchanged during this dispatch, so threads agree on the append offset.
4. Publish the new count and write one `IndirectDrawArguments` record on the GPU.

The render pass draws the logo and six-vertex additive billboard instances into
an RGBA16F target. `drawIndirect` consumes the GPU-written live count; the CPU
does not need it to simulate or draw. Half-resolution compute bloom and tone
mapping produce the final image. The direct comparison draws the entire capacity
and clips inactive instances in the vertex shader.

Simulation dispatches currently cover the fixed capacity, with a live-count guard
in the shader. GPU-generated dispatch sizes, trails, and geometry collisions are
possible extensions. Particle ordering after atomic compaction is unspecified;
there is no sorting requirement for additive blending.

## Timing and synchronization

All backend windows receive the same application time, fixed timestep, emitter
settings, and reset seed. Catch-up is capped at eight steps per application frame;
long stalls therefore slow simulation time rather than creating unbounded work.
Pause and minimized windows do not accumulate a catch-up backlog. Identical seeds
do not imply bit-identical floating-point trajectories across APIs or devices.

The shared [GPU profiler](../base/gpu-profiler.md) measures each simulation step,
particle/logo rendering, bloom, and tone mapping. The HUD sums the simulation
steps from the latest completed profiled frame. These GPU values are separate
from application-loop FPS. Timing excludes initialization, counter clears,
telemetry copies, text drawing, and presentation. Unsupported timing displays an
explicit status.

The live count is optional telemetry: every 30 rendered frames, one of three
readback buffers receives a copy. Submission signals a fence; subsequent frames
map only completed copies. Busy slots are skipped, and resets invalidate old
telemetry. This delayed HUD number never determines the draw or dispatch size.

## Motion and appearance

Mouse motion sweeps a brush with a half-world-unit radius along the cursor path.
Particles follow the motion and spread outward, with smooth falloff to zero at
its edge. Motion is distributed across fixed simulation steps and speed is capped
to keep quick sweeps controlled. A stationary cursor adds no disturbance; pause,
reset, resize, and cursor re-entry do not replay stale movement. Bloom strength
is 0.35, a small increase over the original 0.30.

At initialization, `logo-targets.h` samples 8,192 points from the front-facing
logo surface, weighted by projected triangle area. The samples retain the logo's
material colors and are uploaded once. Each spawned particle receives a target
and style seed that stay with it through compaction; buffer indices never define
its identity. A damped spring assembles the logo while a small oscillation keeps
the held shape alive. A single radial/tangential impulse releases the particles
at the burst; the flow gradually catches them afterward.

The flow combines a broad vortex with the analytic curl of three traveling
scalar waves. It needs no noise textures or particle-neighbor searches. The
curl field is divergence free; the overall vortex and formation forces are not.

Sprites stretch backward along velocity, with capped length, a safe direction
for stationary particles, and reduced brightness as they stretch. Stable style
seeds vary their size and brightness. Settled particles dim to preserve the
orange/cyan silhouette at high density. The backdrop logo is deliberately faint.
These are instantaneous velocity streaks, not stored trajectory trails. Rendering
remains planar with additive blending; there is no depth sorting or collision.

Choreography advances only with simulation steps. Pause, single-step, reset,
and minimized-window behavior apply to both particles and the cycle. Pressing
`F` while paused queues the burst until a single step or resume.

## Backend support

- D3D11, D3D12, Vulkan, and native WebGPU use indirect drawing. D3D11 omits
  live-count telemetry because its RHI fence implementation is unavailable.
- Metal uses the direct-draw fallback; it has not been validated on this Windows
  development machine.
- CPU and CUDA are skipped because this example requires rasterization and a surface.
- HDR render-target, sampling, and storage-write support are required. Timing is
  enabled only when the device advertises timestamp queries and a timestamp frequency.

For useful performance comparisons, use the same particle population, settings,
resolution, and simulation-step count, and run one backend at a time. Concurrent
windows can contend for the same GPU.

## Tests

```sh
./build/Debug/slang-rhi-tests -tc="example-particles*,example-gpu-profiler*"
```

Tests execute the actual simulation and rendering shaders. They cover empty and
full buffers, capacity overflow, compaction and expiry, seeded reset, partially
occupied direct/indirect equivalence, bloom, odd dimensions, and a 1x1 target.
Additional checks cover deterministic logo sampling, both material regions,
stationary/swept mouse interaction and its local falloff, formation convergence,
outward burst velocity and dispersal, cycle wrapping,
manual bursts, and reset. Formed-logo rendering also checks direct/indirect
equivalence.
Profiler tests exercise ring exhaustion, query-slot reuse, and incomplete regions.

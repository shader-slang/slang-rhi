# GPU particle vortex

Two rotating emitters send orange and cyan particles into a vortex around the
Slang logo. Mouse attraction and repulsion disturb the flow. The example uses
procedural sprites and the existing logo mesh, bloom shader, tone mapper, and
bitmap-text renderer; it needs no new assets.

```sh
cmake --build build --config Debug --target example-particles
./build/Debug/example-particles --device vulkan
```

On Windows the executable has an `.exe` suffix. Omit `--device` for side-by-side
backend windows with mirrored controls.

| Control | Action |
|---|---|
| Left mouse | Attract particles |
| Right mouse | Repel particles |
| `[` / `]` | Decrease / increase emission, including zero |
| Space | Pause / resume simulation |
| `N` | Advance one simulation step while paused |
| `R` | Empty the cloud and reset the emitter seed/time |
| `B` | Toggle bloom |
| `D` | Toggle indirect / capacity-sized direct drawing |

## GPU data flow

The capacity is 262,144 particles. Each 48-byte particle contains position and
age, velocity and lifetime, and color and sprite size. Two structured buffers
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
Profiler tests exercise ring exhaustion, query-slot reuse, and incomplete regions.

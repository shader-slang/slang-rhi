# Examples

These examples demonstrate slang-rhi resource creation, shader binding, and GPU
command recording across graphics backends. Start with `surface` or `triangle`
for the basics, then explore the larger rendering examples.

| Example | Demonstrates |
| --- | --- |
| [surface](surface/example-surface.cpp) | Creating a window and surface, clearing an image, and presenting it. |
| [triangle](triangle/example-triangle.cpp) | Vertex buffers, a graphics pipeline, and a triangle draw. |
| [shader-toy](shader-toy/example-shader-toy.cpp) | Rendering procedural images with compute shaders; use the arrow keys to switch shaders. |
| [raster](raster/example-raster.cpp) | A Slang logo and instanced spheres with shadow mapping, environment lighting, MSAA, bloom, and tone mapping. |
| [particles](particles/example-particles.cpp) | GPU particle simulation, indirect drawing, mouse interaction, and bloom around the Slang logo. |
| [path-tracer](path-tracer/example-path-tracer.cpp) | Progressive path tracing with acceleration structures, ray tracing pipelines or ray queries, and sample accumulation. |

## Build and run

From the repository root:

```sh
cmake --preset default
cmake --build build --config Debug --target example-raster
./build/Debug/example-raster --device vulkan
```

Replace `example-raster` with any `example-<name>` target listed above. Examples
are enabled by `SLANG_RHI_BUILD_EXAMPLES`. On Windows, executables have an `.exe`
suffix; use a developer shell when building with MSVC.

`--device` selects `d3d11`, `d3d12`, `vulkan`, `metal`, `cpu`, `cuda`, or `wgpu`,
subject to platform, build configuration, and the example's required features.
The raster example requires rasterization and compute support; the path tracer
requires acceleration structures and ray tracing or ray query support.

Omit `--device` to open windows for the supported backends. Input is mirrored
across windows, and frames are synchronized by default. Use `--free-running`
to let each backend advance independently.

Examples with an overlay display their controls on screen. `F1` toggles the
overlay, `F2` fits the windows to the primary example's monitor, `F3` toggles VSync
across the backend windows, and `Esc` closes the examples. In particles, raster,
path tracer, and shader toy, `F4` toggles profiling across backend windows.

Every desktop window title shows the example, backend, adapter, FPS, and VSync
preference. VSync defaults to on. The preference survives resizing and minimizing. Presentation
mode availability can cause a backend to fall back to synchronized presentation
even when VSync is off.

FPS measures the application frame rate, including presentation and synchronization
waits, and refreshes up to four times per second. Synchronized windows advance at
the rate of the slowest backend; use `--free-running` for independent frame rates.

## Profiling

Particles, raster, path tracer, and shader toy display optional timings for named
rendering regions. Results arrive asynchronously; no extra queue waits are added.
The overlay combines repeated regions per frame and refreshes averaged timings
approximately four times per second. F4 enables/disables measurement independently
of F1 overlay visibility. Unsupported backends continue rendering with timing
unavailable. CPU backend timings are labeled execution time.

These intervals exclude the overlay and presentation, and are separate from the
application FPS in the title. See [GPU timing](base/gpu-profiler.md) for region
boundaries and how to instrument another example.

## Source layout

Each example's `example-<name>.cpp` is its entry point, with Slang shaders next
to it. The [base](base) directory provides shared windowing, camera, geometry,
text, and profiling helpers. The bundled logo's source and regeneration steps
are documented in [base/assets/generate-logo.py](base/assets/generate-logo.py).

The [wasm](wasm) directory contains a separate WebGPU browser test, built as
`wasm-test` with the `emscripten` preset.

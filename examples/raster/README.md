# Slang logo raster example

A beveled, extruded Slang logo on a procedurally checkered ground plane, with
metallic/roughness materials, an orbit camera, and one movable directional light.
The checker uses pixel-footprint filtering to avoid distant aliasing. The logo
uses angle-weighted smooth normals across edges below 30 degrees, preserving
sharp cap and bevel boundaries.

The frame records a depth-only shadow pass, an HDR scene pass with 3x3 PCF
shadow filtering, optional half-resolution compute bloom (highlight extraction
and separable blur), a fullscreen tone-mapping pass, and a bitmap-text overlay.
Shader cursors bind
frame and material constants, textures, and samplers. Named debug groups make
the passes easy to identify in a GPU capture. All intermediate resources remain
on the GPU; slang-rhi tracks their dependencies between passes.

## Build and run

```sh
cmake --preset default
cmake --build build --config Debug --target example-raster
./build/Debug/example-raster --device vulkan
```

On Windows, the executable has the `.exe` suffix. Omit `--device` to open
side-by-side windows on the available raster backends. Mouse and keyboard
controls are mirrored across windows by the example framework.

| Input | Action |
|---|---|
| Left drag | Orbit camera |
| Right drag | Change light direction |
| Mouse wheel | Zoom |
| `[` / `]` | Decrease / increase logo roughness |
| `-` / `=` | Decrease / increase exposure by 0.25 stops |
| `B` | Toggle bloom |
| `R` | Reset camera, light, and material/post settings |

The window title and overlay display the backend, FPS, frame time, roughness,
exposure, and bloom state; the overlay also lists the controls. FPS is based on a
rolling window of 120 application-loop intervals, published at most four times
per second. When several backend windows run together, this measures the shared
application loop, not each backend's GPU execution time. Timing restarts on resize
and restore so minimized time is excluded.
The main window can be resized; rendering pauses while minimized and targets
are recreated when restored.

## Requirements and scope

The example requires a surface and rasterization, sampled D32 depth, and RGBA16F
render-target, sampling, and storage-write support. CPU and CUDA are skipped
because they do not provide rasterization. The shared launcher currently
excludes WebGPU. D3D11, D3D12, and Vulkan have been tested on Windows; Metal
uses the same rendering path but has not been validated on this machine.

Lighting uses a GGX direct-light BRDF and a simple hemispherical ambient term.
There is no environment-map preprocessing, anti-aliasing pass, asset-loading
framework, or ray tracing. Bloom is a small three-dispatch filter, not a mip
pyramid. Tone mapping includes the appropriate linear-to-sRGB conversion for
the output format.

## Shared geometry

`../base/logo-scene.h` exposes ordinary CPU vertex/index arrays, mesh ranges,
and materials independently of the renderer. Positions use world space with
Y up and the logo facing +Z. These arrays can also feed a triangle acceleration
structure in the path tracer; that integration is left for a later change.

The bundled mesh is generated offline from the official SVG. Building or
running the example requires no Python dependencies. See
[`../base/assets/README.md`](../base/assets/README.md) for its source and
regeneration instructions.

## Text helper

`../base/text-renderer.h` provides a reusable informational overlay: a small
hand-defined 5x7 bitmap font, one R8 atlas, and alpha-blended triangle batches.
Supply a program compiled from `../base/text.slang`, then call `clear()`,
`addText()`, and `render()` on the output after tone mapping. Coordinates are in
framebuffer pixels; the example scales the font for the window's pixel density.
Rendering loads the existing output and clips text at its boundaries.

The helper supports uppercase ASCII, digits, common punctuation, spaces, tabs,
and newlines. Lowercase is displayed as uppercase; unsupported characters use
`?`. A batch holds up to 4,096 non-space glyphs, with capacity errors reported
before modifying it. It has no external font assets or runtime dependencies.

## Tests

```sh
./build/Debug/slang-rhi-tests -tc="example-raster-*"
./build/Debug/slang-rhi-tests -tc="example-text-*,example-frame-stats"
```

The tests validate mesh indices, normals, winding, watertightness, and smoothing
across shallow edges while retaining creases, then render the actual
passes offscreen on the enabled raster backends. They check both logo colors,
bloom and exposure changes, light movement, odd dimensions, and a 1x1 target.
Additional tests check rolling frame timing and the text renderer's atlas,
alpha blending, target preservation, multiline layout, buffer reuse, fallback
glyphs, and sRGB/non-sRGB output paths.

# Slang logo raster example

A beveled, extruded Slang logo on a procedurally checkered ground plane, with
metallic/roughness materials, an orbit camera, one movable directional light,
and procedural studio/outdoor environments supplying the background and lighting.
Thirty instanced spheres flank the logo, showing a range of roughness, metallic
values, and colors. The initial camera frames the complete arrangement.
Switchable 4x MSAA smooths geometry edges before HDR post-processing.
The checker uses pixel-footprint filtering to avoid distant aliasing. The logo
uses angle-weighted smooth normals across edges below 30 degrees, preserving
sharp cap and bevel boundaries.

The frame records a depth-only shadow pass, a procedural background pass,
an HDR scene pass with 3x3 PCF shadow filtering, optional half-resolution compute
bloom (highlight extraction and separable blur), a fullscreen tone-mapping pass,
and a bitmap-text overlay.
With MSAA enabled, the scene pass resolves its multisampled color attachment into
the single-sample HDR texture before bloom and tone mapping.
Shader cursors bind
frame and material constants, textures, and samplers. Named debug groups make
the passes easy to identify in a GPU capture. All intermediate resources remain
on the GPU; slang-rhi tracks their dependencies between passes.

## Build and run

```sh
cmake --preset default
cmake --build build --config Debug --target example-raster
./build/Debug/example-raster --device vulkan
./build/Debug/example-raster --device wgpu
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
| `M` | Toggle 4x MSAA / off (when available) |
| `S` | Show / hide the instanced spheres |
| `E` | Switch studio / outdoor environment |
| `,` / `.` | Rotate environment |
| `I` | Toggle environment lighting (background stays visible) |
| `R` | Reset camera, light, material, environment, and post settings |

The window title and overlay display the backend, FPS, frame time, roughness,
exposure, bloom and MSAA state, sphere count, environment preset, and environment-lighting state; the
overlay also lists the controls and environment angle. FPS is based on a
rolling window of 120 application-loop intervals, published at most four times
per second. When several backend windows run together, this measures the shared
application loop, not each backend's GPU execution time. Timing restarts on resize
and restore so minimized time is excluded.
The main window can be resized; rendering pauses while minimized and targets
are recreated when restored.

## Requirements and scope

The example requires a surface and rasterization, sampled D32 depth, and RGBA16F
render-target, sampling, and storage-write support. CPU and CUDA are skipped
because they do not provide rasterization. WebGPU is available explicitly with
`--device wgpu`; the launcher's default backend list does not include it yet.
D3D11, D3D12, Vulkan, and native WebGPU (Dawn) have been tested on Windows; Metal
uses the same rendering path but has not been validated on this machine.

The shaders declare the shadow map as `DepthTexture2D` and the bloom output as
`[format("rgba16f")] WTexture2D<float4>` so WebGPU can validate depth sampling
and the storage texture's format and access mode.

Lighting uses a GGX direct-light BRDF and prefiltered image-based lighting.
There is no asset-loading framework or ray tracing.
Bloom is a small three-dispatch filter, not a mip pyramid.
Tone mapping includes the appropriate linear-to-sRGB conversion for
the output format.

## Multisampling

4x MSAA is enabled by default when available. The renderer checks HDR resolve
support, then probes creation of 4x RGBA16F color and D32 depth attachments and
matching pipelines. If initialization fails, it retains single-sample rendering
and displays `MSAA UNAVAILABLE` in the overlay and title.

Both the background and geometry render into the multisampled HDR color target.
The background pass stores its samples for the geometry pass to load; only the
completed scene is resolved. Bloom, tone mapping, and text rendering use
single-sample targets. The shadow map remains single-sampled with PCF filtering.

Both sets of attachments and pipelines are retained, so `M` toggles immediately
without allocations or a queue wait. Resizing recreates the frame-sized targets
for both modes after waiting for prior submissions. This increases render-target
memory use even while MSAA is off. `R` restores the default MSAA setting.

MSAA improves geometry coverage at silhouette and intersection edges. It does not
supersample material shading or remove all specular/shadow aliasing.

## Instanced spheres

`spheres.h` generates one indexed unit-sphere mesh with smooth normals and a
6-column, 5-row grid of instance records. The grid is split into two groups around
the logo. Roughness increases from 0.08 to 1 across X; metallic increases from 0
in the front row to 1 at the back. Colors transition from orange to cyan across
the columns. The `[` / `]` controls continue to adjust only the logo's roughness.

Each record contains a translation and uniform scale, base color and metallic
value, and roughness, packed into three `float4`s. The RHI creates one shared
vertex buffer, one shared index buffer, and a 48-byte-stride structured buffer
for all instances. Vertex shaders index that buffer with `SV_InstanceID` and
transform the shared mesh. Material attributes feed the same fragment shader
used by the logo and ground.

All 30 spheres are drawn with one `drawIndexed` call with `instanceCount = 30`
in the HDR scene pass, plus one instanced draw in the shadow pass. The spheres
cast and receive shadows and participate in environment lighting, MSAA, and bloom.
The shadow projection covers the larger scene. `S` skips both sphere draws without
recreating resources, and `R` restores their visibility. Instance data is static;
animation, culling, and indirect drawing are left for later extensions.

## Procedural environments

`environment-common.slang` defines a world-direction-to-HDR-color function shared
by the background and lighting preprocessing. Studio mode uses a neutral gradient
and three soft rectangular light panels; outdoor mode uses a blue sky, pale horizon,
and darker lower hemisphere. All environment content is generated by shaders;
there are no environment images or asset-loading dependencies.

At startup, `environment.h` runs compute shaders from `environment.slang` to build
both presets and a shared BRDF lookup texture:

- 256x256 specular cubemaps with nine mip levels. Level zero stores the original
  radiance; the other levels use GGX importance sampling at increasing roughness.
- 16x16 diffuse cubemaps containing cosine-weighted irradiance divided by pi.
- A 128x128 BRDF integration texture indexed by view angle and roughness.

Filtering uses 256 deterministic samples per texel. It evaluates the procedural
function directly, avoiding source cubemap seams during integration. Compute writes
six-layer 2D array textures, which are copied into sampled cubemaps so the same
path works on backends without writable cubemap views. Initialization submits and
waits once before releasing the temporary textures. Preset switching and resize
reuse the generated maps; rotation changes sampling directions without refiltering.
The sky is rotationally symmetric around the vertical axis, so rotation mainly
demonstrates the studio panels.

The background reconstructs world-space viewing directions in a fullscreen pass
before geometry and shares the scene's HDR exposure, bloom, and tone mapping.
Outdoor mode adds an antialiased sun disk aligned with the movable directional
light. That light supplies the sun's direct illumination and shadows; the disk
is excluded from diffuse and specular environment maps to avoid counting it twice.

Environment lighting represents distant illumination: it does not capture the
logo or ground, supply local reflections, or account for local occlusion. The
procedural sky is an artistic gradient, not an atmospheric-scattering simulation.

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
./build/Debug/slang-rhi-tests -tc="resolve-resource-*"
./build/Debug/slang-rhi-tests -tc="example-text-*,example-frame-stats"
```

The tests validate mesh indices, normals, winding, watertightness, and smoothing
across shallow edges while retaining creases, then render the actual
passes offscreen on the enabled raster backends. They check both logo colors,
bloom and exposure changes, light movement, environment switching and rotation,
roughness, lighting on/off, odd dimensions, and a 1x1 target in both single-sample
and 4x MSAA modes. They also toggle MSAA without recreating targets and verify
matching output when switching back (within one 8-bit step for MSAA roundoff).
Sphere checks validate closed geometry, outward winding, normals, non-overlapping
instances, visibility toggling, and rendered pixels at every instance's projected
center. Draw tests also cover switching mesh vertex buffers within a render pass.
Resolve tests verify solid colors and
fractional edge coverage, including on WebGPU. Separate environment
checks sample directions across the sphere to verify cubemap orientation, all mip
levels, finite radiance, and the integrated BRDF's energy bound.
Additional tests check rolling frame timing and the text renderer's atlas,
alpha blending, target preservation, multiline layout, buffer reuse, fallback
glyphs, and sRGB/non-sRGB output paths.

<!--
SPDX-FileCopyrightText: The Khronos Group, Inc.
SPDX-License-Identifier: CC-BY-4.0
-->

# Building and testing

## Prerequisites

- Git and a C++20 compiler (MSVC, Clang, or GCC).
- CMake 3.25 or newer for the version 6 preset file, and Ninja for the default,
  `msvc`, `clang`, and `gcc` presets. The CMake project itself requires 3.20.
- Network access during initial configuration to fetch Slang and other dependencies.
- Drivers and runtimes for the backends you intend to use.

On Windows, use a Visual Studio developer shell with the C++ tools and Windows
SDK available. On macOS, install the Xcode command line tools. On Linux, the
default examples and windowed tests also require X11 development packages
(`xorg-dev` on Ubuntu, as used by CI).

## Configure and build

From the repository root:

```sh
cmake --preset default
cmake --build build --config Debug
```

The default preset uses Ninja Multi-Config and the compiler available in your
environment. Use `msvc`, `clang`, or `gcc` instead of `default` to select a
compiler explicitly. See [CMakePresets.json](../CMakePresets.json) for additional
presets. Use a separate build directory when switching compilers, for example
`cmake --preset clang -B build-clang`.

Build configurations include `Debug`, `Release`, and `RelWithDebInfo`. With the
default preset, executables are placed in `build/<configuration>/`.

### WebAssembly

Activate the Emscripten SDK, then run from the repository root:

```sh
cmake --preset emscripten
cmake --build build --config Debug --target wasm-test
python -m http.server 8000 --bind 127.0.0.1 --directory build/Debug
```

Open `http://localhost:8000/wasm-test.html` in a browser with WebGPU and JSPI support.
The preset uses `-sJSPI`; additional JavaScript-facing exports that can wait must
be listed in `-sJSPI_EXPORTS` and their returned promises awaited (`main` is included by default).

Asyncify is a historical, unsupported alternative: replace `-sJSPI` with
`-sASYNCIFY -sBINARYEN_EXTRA_PASSES=--optimize-level=0`, restore the `wgpu::wait`
polling loop and `ASYNCIFY_ONLY` allowlist from Git history, and remove the example's
JSPI browser check. Adapt the allowlist to cover every caller on a suspending call
chain, including application code and indirect callers.

## Common options

Pass options when configuring, for example:

```sh
cmake --preset default -DSLANG_RHI_BUILD_EXAMPLES=OFF
```

| Option | Default | Purpose |
|--------|---------|---------|
| `SLANG_RHI_BUILD_TESTS` | `ON` for standalone builds | Build the test executable. Requires a static, non-Emscripten build. |
| `SLANG_RHI_BUILD_TESTS_WITH_GLFW` | `ON` for standalone builds | Include tests that require GLFW. |
| `SLANG_RHI_BUILD_EXAMPLES` | `ON` for standalone builds | Build example applications. |
| `SLANG_RHI_BUILD_SHARED` | `OFF` | Build a shared library instead of a static library. |
| `SLANG_RHI_ENABLE_<BACKEND>` | `ON` where available | Enable `CPU`, `D3D11`, `D3D12`, `VULKAN`, `METAL`, `CUDA`, or `WGPU`. |
| `SLANG_RHI_FETCH_SLANG` | `ON` | Download the Slang version selected by the build configuration. |

For a build without GLFW dependencies, disable both `SLANG_RHI_BUILD_EXAMPLES`
and `SLANG_RHI_BUILD_TESTS_WITH_GLFW`. To supply Slang yourself, disable
`SLANG_RHI_FETCH_SLANG` and set `SLANG_RHI_SLANG_INCLUDE_DIR` to its include
directory and `SLANG_RHI_SLANG_BINARY_DIR` to the directory containing its `bin/`
and `lib/` directories. Use a Slang version compatible with this checkout.

See [CMakeLists.txt](../CMakeLists.txt) for the full set of options and the
selected dependency versions.

The Windows D3D12 build uses DXC 1.10.2605.24 and Agility SDK
1.721.3-preview for Shader Model 6.10 Linear Algebra (cooperative vectors).
Executables select the bundled runtime by including `slang-rhi/agility-sdk.h`
and placing `SLANG_RHI_EXPORT_AGILITY_SDK` at file scope. Applications using the
preview shader model must also enable
`D3D12ExperimentalShaderModels` before device creation, using
`D3D12ExperimentalFeaturesDesc`. The neural marble example does this explicitly;
other examples do not enable experimental shader models automatically. Deploy
the `hlsl/` directory alongside DXC and the executable/library: Slang's generated
LinAlg shaders include DXC's `dx/linalg.h` at runtime. CMake copies and installs
these headers with the compiler.

To override the dependencies with a retail Agility SDK, also set
`SLANG_RHI_AGILITY_SDK_VERSION=D3D12_SDK_VERSION`. A retail configuration does not
enable the SM 6.10 cooperative-vector path.

## Run tests and examples

Run from the build output directory:

```sh
cd build/Debug
./slang-rhi-tests -check-devices
./example-triangle
./example-runner
```

On Windows, append `.exe` to executable names. Tests and examples need the
corresponding build options enabled; windowed examples also need a display and
a backend that supports rendering and presentation.

Desktop builds produce individual binaries such as `example-triangle` and a
combined `example-runner`.

GPU test names end in the backend name. Use doctest's test-case filter to select
tests, or select a backend explicitly:

```sh
./slang-rhi-tests -tc="*.vulkan"
./slang-rhi-tests -select-devices=vulkan -check-devices -require-devices=vulkan
```

`-check-devices` checks backend availability before running tests;
`-require-devices=vulkan` makes the run fail if Vulkan is unavailable. Tests for
unsupported devices or features may otherwise be skipped.

The [CI workflow](../.github/workflows/ci.yml) records the compiler, platform,
and backend combinations exercised by the project.

### Neural marble example

Build `example-neural-marble` and run it with `--device vulkan` to watch a small
network learn a procedural navy-and-gold marble texture from random weights:

```sh
cmake --build build --config Debug --target example-neural-marble
./build/Debug/example-neural-marble --device vulkan
```

It is also available in `example-runner --example neural-marble`, or through the
combined runner's F5 example picker.

The three panels show the procedural reference, learned RGB texture, and RGB
root-mean-square error with a fixed 0–0.25 color scale. Training starts immediately.
The overlay reports delayed validation MSE on a fixed grid, optimizer steps,
and separate GPU timings for matrix preparation, training, and inference.

| Key | Action |
|-----|--------|
| Space | Pause/resume training. |
| R | Reset grid features, MLP weights, Adam moments, and step count; preserve the current target and pause state. |
| G | Change the marble seed, vein frequency, and turbulence; preserve the grid, MLP, and optimizer. |
| C | Switch cooperative-vector/ordinary-math execution when available, preserving training state. |

The shared F1–F4 and Escape shortcuts control the overlay, window layout, VSync,
profiler, and exit. There are no mouse controls or external texture/model assets.

The UV encoding is a trainable multiresolution 2D hash grid inspired by
[instant-NGP](https://nvlabs.github.io/instant-ngp/). Eight levels span 8 to 256
cells per axis, with two features per grid vertex. Coarse levels use dense
storage; finer levels hash vertices into 4,096 entries each. Bilinear sampling
produces 16 inputs for two 64-wide hidden layers and four outputs (RGB plus
padding). The MLP has 5,508 parameters; the grid adds 35,742 trainable features.

Slang autodiff differentiates the bilinear interpolation, network, and RGB loss.
Custom derivatives connect grid loads and linear layers to shared parameter
storage. Each sample writes four gradient nodes per grid level; integer atomic
exchanges link nodes sharing an entry, including hash collisions. An optimizer
pass sums each entry's list and applies Adam. This avoids float atomics and a
dense per-sample grid-gradient buffer. The MLP uses per-sample gradients reduced
in a separate pass. Both optimizers decay moments even for zero gradients. The
cooperative-vector path uses FP16 matrix-vector operations in the forward and
backward passes, while master weights, gradients, and optimizer state stay FP32.
RHI matrix-size queries and GPU layout conversion prepare inference-optimal
matrices after updates. Training samples the entire UV domain independently of
the fixed-resolution preview. Validation uses a 128-by-128 grid to capture fine
vein errors. Adjust grid levels, resolutions, table capacity, network width,
learning rates, batch size, and steps per frame in
[settings.h](../examples/neural-marble/settings.h). Wider networks also increase
per-sample gradient storage and training cost; compare convergence at equal
elapsed training time when tuning these settings.

Cooperative execution is enabled on Vulkan and D3D12 devices advertising the
required FP16 configuration. Run `example-neural-marble --device d3d12` for the
DirectX path, which requires SM 6.10 support (NVIDIA R615 or newer). D3D12 checks
native Linear Algebra operation support and retains NVAPI matrix conversion.
The overlay reports `Cooperative vectors` when that path is active; unsupported
devices use ordinary math. CUDA, Metal, and WebGPU also use ordinary math;
Slang's native CUDA buffer overload does not emit the correct matrix address.
Direct3D requires Shader Model 6/DXC; the example skips D3D11 because FXC cannot
efficiently compile the differentiated arrays.

The headless regressions check grid gradients against finite differences at
domain boundaries and with forced hash collisions, learning across multiple
marble seeds, adaptation, cooperative-mode parity where supported, and reset
of both parameter sets and optimizers. Shared-entry sums can differ slightly
with GPU scheduling, so grid update comparisons allow floating-point roundoff:

```sh
./build/Debug/slang-rhi-tests -tc="neural-marble-*"
```

To exercise SM 6.10 explicitly, including the cooperative-vector conversion
tests, use `-select-devices=d3d12 -require-devices=d3d12
-d3d12-shader-model=6.10`. The test harness enables experimental shader models
for this selection before creating any devices. Its default D3D12 suite remains
capped at SM 6.8; run the SM 6.10 configuration separately to check marble's
cooperative execution path. Enabling experimental features after devices have
already been created can invalidate those devices.

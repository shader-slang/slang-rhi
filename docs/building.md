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

## Run tests and examples

Run from the build output directory:

```sh
cd build/Debug
./slang-rhi-tests -check-devices
./example-triangle
```

On Windows, append `.exe` to executable names. Tests and examples need the
corresponding build options enabled; windowed examples also need a display and
a backend that supports rendering and presentation.

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

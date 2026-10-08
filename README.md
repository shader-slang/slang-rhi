<!--
SPDX-FileCopyrightText: The Khronos Group, Inc.
SPDX-License-Identifier: CC-BY-4.0
-->

[![CI](https://github.com/shader-slang/slang-rhi/actions/workflows/ci.yml/badge.svg)](https://github.com/shader-slang/slang-rhi/actions/workflows/ci.yml)
[![Sanitizers](https://github.com/shader-slang/slang-rhi/actions/workflows/sanitizers.yml/badge.svg?branch=main)](https://github.com/shader-slang/slang-rhi/actions/workflows/sanitizers.yml)
[![Coverage Status](https://coveralls.io/repos/github/shader-slang/slang-rhi/badge.svg?branch=main)](https://coveralls.io/github/shader-slang/slang-rhi?branch=main)

# slang-rhi

`slang-rhi` is a C++ render hardware interface for the Slang shading language,
originally based on Slang's "gfx" layer. It provides a common API for resource
management, shader binding, and graphics, compute, and ray tracing workloads.

Backends include Direct3D 11, Direct3D 12, Vulkan, Metal, CUDA, WebGPU, and CPU.
Availability and feature support vary by platform and device; see the
[API implementation status](docs/api.md) and query device capabilities at runtime.

## Project status and compatibility

`slang-rhi` is under active development. The public API is free to change, and
neither source compatibility nor binary compatibility (ABI) is currently
guaranteed between revisions or releases. When updating, expect to rebuild
dependent code and adapt it to API changes. Use headers and binaries from the
same revision.

## Getting started

You need Git, a C++20 compiler, CMake 3.25 or newer for the presets, and Ninja.
On Windows, run from a Visual Studio developer shell. See the
[build guide](docs/building.md) for platform prerequisites and configuration options.

```sh
git clone https://github.com/shader-slang/slang-rhi.git
cd slang-rhi
cmake --preset default
cmake --build build --config Debug
```

The default standalone build includes tests and examples, and downloads Slang
and other required dependencies during configuration. Try the
[triangle example](examples/triangle/example-triangle.cpp) or run the tests:

```sh
cd build/Debug
./example-triangle
./slang-rhi-tests -check-devices
```

On Windows, append `.exe` to executable names.

## Documentation

- [Building and testing](docs/building.md): prerequisites, CMake options, and test selection.
- [API implementation status](docs/api.md): coverage across backends.
- [Error handling and diagnostics](docs/error-handling.md): result codes and debug callbacks.
- [Object lifetime](docs/object-lifetime.md): reference counting and device ownership.
- [Synthetic resource bindings](docs/synthetic-bindings.md): compiler-generated shader resources.
- [Public API](include/slang-rhi.h): interfaces, descriptors, and API comments.
- [Examples](examples): complete applications using the API.

For contributions, see [CONTRIBUTING.md](CONTRIBUTING.md). Report bugs and request
features through [GitHub issues](https://github.com/shader-slang/slang-rhi/issues).

## License

`slang-rhi` is released under the Apache 2.0 with LLVM Exception license. See the file  [LICENSE](LICENSE) for more information.

`slang-rhi` depends on the following third-party libraries, which have their own license:

- [doctest](https://github.com/doctest/doctest) (MIT)
- [metal-cpp](https://developer.apple.com/metal/cpp) (Apache 2.0)
- [RenderDoc API](https://github.com/baldurk/renderdoc) (MIT)
- [stb](https://github.com/nothings/stb) (Public Domain)
- [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) (MIT)
- [OffsetAllocator](https://github.com/sebbbi/OffsetAllocator) (MIT)
- [WinPixEventRuntime](https://www.nuget.org/packages/WinPixEventRuntime) (MIT)
- [D3D12 Memory Allocator](https://gpuopen.com/d3d12-memory-allocator) (MIT)
- [Vulkan Memory Allocator](https://gpuopen.com/vulkan-memory-allocator) (MIT)

<!--
SPDX-FileCopyrightText: The Khronos Group, Inc.
SPDX-License-Identifier: CC-BY-4.0
-->

[![CI](https://github.com/shader-slang/slang-rhi/actions/workflows/ci.yml/badge.svg)](https://github.com/shader-slang/slang-rhi/actions/workflows/ci.yml)
[![Coverage Status](https://coveralls.io/repos/github/shader-slang/slang-rhi/badge.svg?branch=main)](https://coveralls.io/github/shader-slang/slang-rhi?branch=main)

# slang-rhi

## Introduction

The `slang-rhi` library provides a render hardware interface for the Slang shading language.
It is based on the "gfx" layer originally developed in the Slang repository.
This library is under active refactoring and development, and is not yet ready for general use.

## Building a shared library

Configure with `-DSLANG_RHI_BUILD_SHARED=ON` (the default is a static library):

```sh
cmake --preset default -B build-shared -DSLANG_RHI_BUILD_SHARED=ON
cmake --build build-shared --config Release
cmake --install build-shared --config Release --prefix install
```

Use the generated `slang-rhi-config.h` matching your library, together with the
Slang SDK headers and link library. Windows installations include the DLL in `bin` and its import
library in `lib`. Unix installations put the shared library and its bundled
runtime dependencies together in the library directory. If Slang is supplied by
the parent project rather than fetched, that project must provide its runtime.

`slang-rhi-public-api-tests` exercises the public API in both static and shared
builds. It defaults to CPU when enabled; pass backend names such as `vulkan wgpu`
to require those devices. The existing `slang-rhi-tests` suite accesses internal
implementation details and remains available only in static builds. To check an
independently built consumer against a relocated installation:

```sh
python tools/test-installed-library.py --build-dir build-shared --config Release --devices cpu
```

The loader functions and `cu*` globals in `slang-rhi/cuda-driver-api.h` are only
supported when linking statically. The CUDA backend and public OptiX denoiser
interface are supported by shared builds when enabled. Keep the client headers and library
from the same revision; this project does not promise a stable binary ABI.

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

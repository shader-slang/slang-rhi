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

## CUDA test compiler selection

The CUDA tests use the linked Slang compiler's default CUDA compilation path unless a compiler is
selected explicitly. To compare the NVRTC and NVVM paths, run separate processes:

```bash
./build/Debug/slang-rhi-tests --select-devices=cuda --require-devices=cuda --cuda-compiler=nvrtc --test-case='*.cuda'
./build/Debug/slang-rhi-tests --select-devices=cuda --require-devices=cuda --cuda-compiler=nvvm --test-case='*.cuda'
```

Use a narrower test filter, such as `--test-case=compute-trivial.cuda`, for a smoke test. These are
on-demand runs; selecting NVVM does not imply that every CUDA test is supported by that compiler.
`--require-devices=cuda` makes an unavailable CUDA device or a failed compiler availability check an
error. Invalid compiler names and selectors unsupported by the linked Slang compiler also fail.

The selector applies to the CUDA availability shader and devices created through the shared
`createTestingDevice` helper, including its cached devices. Tests that construct their own
`DeviceDesc` or Slang sessions must forward the options themselves; shader-cache and
`device-from-handle` tests include such paths. RHI's internal CUDA clear kernels and the standalone
`nvrtc` test use NVRTC directly and are not changed by this selector. Other backends are unchanged.

To use a local Slang build, configure with `SLANG_RHI_FETCH_SLANG=OFF`,
`SLANG_RHI_SLANG_INCLUDE_DIR=/path/to/slang/include`, and
`SLANG_RHI_SLANG_BINARY_DIR=/path/to/slang/build/RelWithDebInfo`. The binary directory is the package
root containing `lib/` and `bin/`. On Linux, make the matching compiler libraries and optional
`libslang-llvm-nvvm.so` provider discoverable (for example, through `LD_LIBRARY_PATH`), along with the
required CUDA toolkit libraries. A configure-time header check preserves default builds with older
Slang versions. Explicit selection requires headers exposing `EmitCUDAMethod` and a linked compiler
that recognizes the corresponding `-emit-cuda-via-nvrtc` or `-emit-cuda-via-nvvm` option. A compile
request validates the runtime's option support before exactly one compiler-selection entry is
forwarded; unrelated per-test compiler options are preserved.

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

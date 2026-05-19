[![CI](https://github.com/shader-slang/slang-rhi/actions/workflows/ci.yml/badge.svg)](https://github.com/shader-slang/slang-rhi/actions/workflows/ci.yml)
[![Coverage Status](https://coveralls.io/repos/github/shader-slang/slang-rhi/badge.svg?branch=main)](https://coveralls.io/github/shader-slang/slang-rhi?branch=main)

# slang-rhi

## Introduction

The `slang-rhi` library provides a render hardware interface for the Slang shading language.
It is based on the "gfx" layer originally developed in the Slang repository.
This library is under active refactoring and development, and is not yet ready for general use.

## Synthetic resource bindings

`slang-rhi` can bind compiler-synthesized resources that do not appear
in normal Slang reflection, such as the hidden coverage counter buffer
emitted by shader coverage.

The public API surface is:

- `ShaderProgramSyntheticResourcesDesc`
  - opt-in descriptor chained through `ShaderProgramDesc.next` when creating a program
- `SyntheticResourceBindingDesc`
  - one record per hidden resource
- `ISyntheticShaderProgram`
  - query resolved binding locations after program creation; ordinary
    programs without synthetic resources do not expose this interface
- `bindSyntheticResource(...)`
  - convenience helper for binding a hidden resource by synthetic
    resource id

The intended flow is:

1. query hidden-resource metadata from Slang, typically through
   `slang::ISyntheticResourceMetadata`
2. translate that into an array of `SyntheticResourceBindingDesc`
3. pass the array into `createShaderProgram()` via
   `ShaderProgramSyntheticResourcesDesc`
4. bind the resource through `bindSyntheticResource(...)` or by
   resolving a `SyntheticBindingLocation` from
   `ISyntheticShaderProgram` and calling `IShaderObject::setBinding()`

The synthetic resource path is intentionally inactive for normal
programs. If `ShaderProgramSyntheticResourcesDesc` is not present,
`createShaderProgram()` follows the ordinary backend path and the
resulting program returns `SLANG_E_NO_INTERFACE` when queried for
`ISyntheticShaderProgram`. Backends that do not support synthetic
resources reject programs that provide synthetic descriptors with
`SLANG_E_NOT_IMPLEMENTED`.

Current backend support for this path:

- Vulkan
- CUDA

See [docs/api.md](docs/api.md) for the interface status tables.

## License

`slang-rhi` is released under the MIT license. See the file  [LICENSE](LICENSE) for more information.

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

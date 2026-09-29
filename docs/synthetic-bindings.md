# Synthetic Resource Bindings

Synthetic resource bindings let `slang-rhi` bind compiler-synthesized
resources that do not appear in normal Slang reflection. The first use
case is Slang shader coverage, where the compiler emits a hidden
counter buffer.

This is an optional API. Applications that do not include
`<slang-rhi/synthetic-bindings.h>` or chain
`ShaderProgramSyntheticResourcesDesc` through `ShaderProgramDesc.next`
do not use this path.

## Public API

Include:

```cpp
#include <slang-rhi/synthetic-bindings.h>
```

The optional API contains:

- `ShaderProgramSyntheticResourcesDesc`: descriptor chained through
  `ShaderProgramDesc.next`.
- `SyntheticResourceBindingDesc`: one resource record supplied by the
  host.
- `ISyntheticShaderProgram`: optional query interface exposed only by
  programs that were created with synthetic resources on supported
  backends.
- `bindSyntheticResource(...)`: helper that resolves a synthetic
  resource id and binds through `IShaderObject::setBinding()`.

## Usage

The intended flow is:

1. Query hidden-resource metadata from Slang, typically through
   `slang::ISyntheticResourceMetadata`.
2. Translate each metadata record into `SyntheticResourceBindingDesc`.
3. Chain `ShaderProgramSyntheticResourcesDesc` through
   `ShaderProgramDesc.next` when creating the shader program.
4. Bind the resource through `bindSyntheticResource(...)`, or query
   `ISyntheticShaderProgram` and call `IShaderObject::setBinding()`
   with the resolved `SyntheticBindingLocation`.

If `ShaderProgramSyntheticResourcesDesc` is omitted, the program uses
the ordinary path and does not expose `ISyntheticShaderProgram`.

Backends that do not support synthetic resources reject non-empty
synthetic descriptors with `SLANG_E_NOT_IMPLEMENTED`.

For coverage buffers, query `ICoverageTracingMetadata::getBufferInfo()` and
use `elementByteWidth` for both the buffer element size and counter readback.
Allocate `getCounterCount() * elementByteWidth` bytes. Slang defaults to
64-bit counters; devices without 64-bit buffer atomics, including MoltenVK,
need an explicit `TraceCoverageCounterByteWidth = 4` compiler option.

CUDA synthetic descriptors must provide enough uniform storage for each
resource: a minimum `uniformStride` of 16 bytes for buffers (pointer and
count), or 8 bytes for texture and acceleration-structure handles.

## Backend Support

Current support:

| Backend | Support |
|---------|---------|
| CUDA    | yes     |
| Vulkan  | yes     |
| CPU     | no      |
| D3D11   | no      |
| D3D12   | no      |
| Metal   | no      |
| WGPU    | no      |

Unsupported backends still behave normally when no synthetic resource
descriptor is provided.

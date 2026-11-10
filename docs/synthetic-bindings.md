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
resource: a minimum `uniformStride` of 16 bytes for raw/structured buffers (pointer and
count), or 8 bytes for texture and acceleration-structure handles.

## Backend Support

Current support:

Vulkan and CUDA currently support only `SyntheticResourceScope::Global`.
`EntryPoint` scope is reserved for future support; creating a program with an
entry-point-scoped synthetic descriptor returns `SLANG_E_NOT_IMPLEMENTED`.
Global scope means the resource is bound on the root shader object, not that
coverage is restricted to any particular function or entry point.

Vulkan supports synthetic buffers, textures, samplers, combined texture/samplers,
and acceleration structures (when supported by the device). CUDA supports raw/structured
buffers, textures, combined texture/samplers, and acceleration structures. CUDA rejects
`TypedBuffer` and `MutableTypedBuffer` with `SLANG_E_NOT_IMPLEMENTED`, regardless of
`uniformStride`: their required texture/surface handles are not implemented by the
buffer binding writer. Coverage uses the supported `MutableRawBuffer` type. Other binding
types return `SLANG_E_NOT_IMPLEMENTED`; `Unknown` is an invalid descriptor.

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

## Vulkan descriptor-set ownership

Synthetic resources must use descriptor sets that are not occupied by ordinary
reflected resources, including resources in global, nested, or entry-point
parameter blocks. Sharing an occupied set is rejected with `SLANG_E_INVALID_ARG`,
even when the requested binding number is unused. Multiple synthetic resources
may share a synthetic-only set at distinct binding numbers; duplicate bindings
are rejected.

Slang's automatic Vulkan coverage placement already selects a separate set.
Applications using an explicit `TraceCoverageBinding` / `-trace-coverage-binding`
override that shares a reflected set must select an unoccupied set and recompile.
The RHI does not relocate bindings: descriptor locations must match the emitted
shader code. Host-reserved sets absent from reflection can be reported to Slang
with `TraceCoverageReservedSpace` / `-trace-coverage-reserved-space` when using
automatic coverage allocation.

Explicit set numbers and gaps are preserved. For example, a parameter block at
set 0 remains at set 0 when a reflected root buffer occupies set 3 and coverage
occupies set 4. A synthetic resource can also occupy a gap that no reflected
object owns.

The pipeline layout borrows each parameter block's native set layouts, and
parameter blocks allocate their own descriptor sets. The root owns its reflected
and synthetic sets, including empty sets that fill gaps through the normal
allocation path. Existing subobject layout records carry relative register-space
offsets, including nested container and entry-point offsets, so sibling traversal
order does not change binding locations.

`src/vulkan/vk-synthetic-bindings.cpp` validates set ownership and adds root-owned
resource ranges for the synthetic sets. Descriptor writes and resource tracking
use the existing resource-slot binding path, without interpreting synthetic IDs
or coverage metadata. An absent or empty extension retains the ordinary layout
path. CUDA synthetic-resource support is unchanged by this Vulkan restriction.

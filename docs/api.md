# API implementation status

## Entry-point uniform layout selection

`ShaderProgramDesc::entryPointUniformLayoutGroupIndex` selects a group from Slang's
experimental entry-point uniform layout constraints. Selection is supported on Vulkan
with `LinkingStyle::SingleProgram`. Its default value, `UINT32_MAX`, preserves ordinary
program behavior; a program containing push-constant layout groups requires an explicit selection. Ray-tracing
shader-record constraints are independent and do not select a graphics push-constant group.

`selectedEntryPointIndices` and `selectedEntryPointCount` optionally select a subset of
the group, using the final Slang program's entry-point indices. An empty subset selects
the entire group. The selected stages use the compiler's reflected push-constant offsets
and upload spans. The root shader object and descriptor layout retain the full program's
index namespace, including unselected entry points. Different subsets retain shader byte
offsets but do not promise compatible native Vulkan pipeline layouts.

## `IDevice` interface

### Ray-tracing shader record parameters

On Vulkan and D3D12, `ShaderProgramDesc::useRayTracingShaderRecordParameters` opts a
ray-tracing program into per-record ordinary entry-point uniforms. Use SingleProgram linking
with shader-record coexistence constraints for multi-stage hit groups on Vulkan. Resources
remain global, or are represented by
explicit `DescriptorHandle` values in the ordinary data. Entry-point resource/sub-object
bindings are rejected. All three hit-group stages may carry independent ordinary uniforms.
`IRayTracingPipeline::getShaderRecordLayout(exportName, &layout)` returns the total application
data size and up to three regions, each identified by program entry-point index, byte offset,
and byte size. Bind each stage with its normal shader object, then copy those bytes into its
region. Vulkan uses Slang's `getShaderRecordOffset()`/`getShaderRecordSize()` reflection;
D3D12 packs DWORD-aligned regions in closest-hit, any-hit, intersection order and combines
those stages' reflected CBV registers in the group's local root signature. Use distinct native
exports for groups requiring different stage layouts. The D3D12 512-byte driver-blocker limit
applies to the combined group.

`ShaderTableDesc` accepts optional `ShaderRecordData` arrays for ray generation, miss, hit,
and callable records. Each array has the corresponding record count. `createShaderTable`
copies the bytes immediately; repeated records of the same shader can therefore have
independent values. `offset` counts from the application data after the native identifier,
and leading/unwritten bytes are zero. Pack values using the target's reflected entry-point
layout. Legacy `ShaderRecordOverwrite` patches apply afterwards. Explicit ray-generation
record data is not replaced with root shader object parameters during dispatch.

The existing shader-object API can pack this data without duplicating the target layout:

```cpp
auto packingRoot = device->createRootShaderObject(program);
auto entryPoint = packingRoot->getEntryPoint(entryPointIndex);
auto capture = ShaderCursor(entryPoint)["environment"];
capture["scene"].setDescriptorHandle(sceneHandle);
capture["bias"].setData(bias);
ShaderRecordData record{entryPoint->getRawData(), uint32_t(entryPoint->getSize()), 0};
```

An individual entry-point packing span starts at zero; place it at the offset returned by
`getShaderRecordLayout` when assembling a hit group. cursors account for
nonzero offsets of individual uniform parameters and nested fields. Keep the packing objects
alive until `createShaderTable` snapshots their bytes. Both Vulkan and D3D12 support
`IAccelerationStructure::getDescriptorHandle`, alongside buffer, texture, and sampler handles,
and bind the device's shared descriptor heaps for ray-tracing pipelines. Descriptor handles
and record bytes do not retain the referenced resources or infer resource transitions: keep
resources alive through execution and explicitly set states for bindless-only resource access.
Acceleration-structure handles follow Slang's backend ABI: Vulkan uses a native device address
and D3D12 uses a descriptor-heap index. Vulkan therefore needs no heap slot for an acceleration
structure; `BindlessDesc::accelerationStructureCount` controls D3D12 capacity.
The usual Slang bindless indexing rules still apply: wrap a handle in `nonuniform(handle)`
when different invocations may select different descriptors. In particular, hit or miss records
for the same shader can hold different resource handles even within one ray dispatch.

Vulkan stores these ordinary bytes in shader-record storage. D3D12 associates reflected local
root constants with raygen/miss/callable exports and hit groups, excluding these uniforms from
the global root signature. Buffer addresses, section offsets, and record strides retain each
backend's alignment rules. Record data exceeding the backend's supported limit is rejected. CUDA,
Metal, and other backends do not currently support these record-data arrays.

Record sizes are computed automatically from the reflected uniform block and the extent of
the supplied data (`offset + size`). Each miss, hit, and callable section has its own aligned
stride, large enough for every record in that section. D3D12 also uses one stride for its raygen
records; Vulkan sizes each raygen record independently. There is no fixed 256-byte capacity.

- D3D12 currently caps **application data at 512 bytes (128 DWORDs)** because of the driver
  blocker described below. This cap excludes the 32-byte shader identifier and record alignment
  padding. Reflected uniform blocks exceeding 512 bytes fail during program creation, before
  native root signature creation. `createShaderTable` also rejects explicit `offset + size`
  extents above 512 bytes. Both diagnostics identify the **D3D12 driver blocker** and the limit.
  The DXR API itself permits 4064 application bytes within a 4096-byte total record; the temporary
  cap is separate from both that limit and the 64-DWORD global root signature limit.
- Vulkan uses the device's `maxShaderGroupStride` and `shaderGroupHandleSize`. RHI currently
  aligns all records to `shaderGroupBaseAlignment`, so the maximum application extent is
  `alignDown(maxShaderGroupStride, shaderGroupBaseAlignment) - shaderGroupHandleSize`.
  Program creation rejects reflected uniform blocks whose aligned records exceed that limit.
  KHR ray-tracing pipelines guarantee at least a 4096-byte maximum stride.

The shader's field types and offsets belong to its compiled layout; D3D12's corresponding
local root signature is associated with the pipeline. Record contents and table strides are
runtime data: a new `ShaderTableDesc` can supply different values or more trailing bytes and
reuse the same pipeline. Extra bytes do not add shader fields. No capacity setting is needed
on the pipeline. `createShaderTable` validates caller data extents before copying, including
overflowing offsets, and reports the offending record and byte limit. Zero-filled padding
and immutable snapshots behave identically for small and large records.

Native contracts: [DXR local root signatures](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#local-root-signatures-vs-global-root-signatures)
and [Vulkan ray-tracing limits](https://docs.vulkan.org/spec/latest/chapters/limits.html).

Driver blocker (September 29, 2026): on an RTX 5090 with driver 610.88, a single local root
constant parameter with 129 DWORDs fails `CreateRootSignature` with device removal; 128 DWORDs
succeeds. Debug validation reports `DXGI_ERROR_DRIVER_INTERNAL_ERROR`. This reproduces with a
standalone D3D12 call, without Slang, shaders, or an SBT. Until this is resolved, the 512-byte
cap applies to all D3D12 devices; no indirect constant-buffer fallback is used. Vulkan retains
its full native limit. Tests exercise successful 512-byte D3D12 uniforms, rejection above the
cap, and 4064-byte Vulkan uniforms, along with runtime strides and pipeline reuse.

| API                                | CPU     | CUDA | D3D11 | D3D12 | Vulkan | Metal   | WGPU |
| ---------------------------------- | ------- | ---- | ----- | ----- | ------ | ------- | ---- |
| `getNativeDeviceHandles`           | :x:     | yes  | :x:   | yes   | yes    | yes     | :x:  |
| `getInfo`                          | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `hasFeature`                       | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `getFeatures`                      | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `getCapabilities`                  | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `hasCapability`                    | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `getFormatSupport`                 | yes     | yes  | yes   | yes   | yes    | yes (1) | yes  |
| `getSlangSession`                  | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createSlangSession`               | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `getQueue`                         | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `trimMemory`                       | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createTexture`                    | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createTextureFromNativeHandle`    | :x:     | :x:  | :x:   | yes   | yes    | yes     | :x:  |
| `createTextureFromSharedHandle`    | :x:     | yes  | :x:   | :x:   | :x:    | :x:     | :x:  |
| `createBuffer`                     | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createBufferFromNativeHandle`     | :x:     | yes  | :x:   | yes   | yes    | yes     | yes  |
| `createBufferFromSharedHandle`     | :x:     | yes  | :x:   | :x:   | :x:    | :x:     | :x:  |
| `mapBuffer`                        | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `unmapBuffer`                      | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createSampler`                    | yes (2) | yes  | yes   | yes   | yes    | yes     | yes  |
| `createTextureView`                | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createSurface`                    | :x:     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createInputLayout`                | :x:     | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createShaderObject`               | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createShaderObjectFromTypeLayout` | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createRootShaderObject`           | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createShaderTable`                | :x:     | yes  | :x:   | yes   | yes    | :x:     | :x:  |
| `createShaderProgram`              | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createRenderPipeline`             | :x:     | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createComputePipeline`            | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createRayTracingPipeline`         | :x:     | yes  | :x:   | yes   | yes    | :x:     | :x:  |
| `readTexture`                      | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `readBuffer`                       | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createQueryPool`                  | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `getAccelerationStructureSizes`    | :x:     | yes  | :x:   | yes   | yes    | yes     | :x:  |
| `getMicromapSizes`                 | :x:     | yes  | :x:   | yes   | yes    | :x:     | :x:  |
| `getClusterOperationSizes`         | :x:     | yes  | :x:   | yes   | yes    | :x:     | :x:  |
| `createAccelerationStructure`      | :x:     | yes  | :x:   | yes   | yes    | yes     | :x:  |
| `createMicromap`                   | :x:     | yes  | :x:   | yes   | yes    | :x:     | :x:  |
| `createFence`                      | yes     | yes  | :x:   | yes   | yes    | yes     | yes  |
| `waitForFences`                    | yes     | yes  | :x:   | yes   | yes    | yes     | yes  |
| `createHeap`                       | :x:     | yes  | :x:   | yes   | yes    | :x:     | :x:  |
| `getTextureAllocationInfo`         | yes     | yes  | :x:   | yes   | yes    | yes     | :x:  |
| `getTextureRowAlignment`           | yes     | yes  | :x:   | yes   | yes    | yes     | yes  |
| `getCooperativeVectorProperties`   | :x:     | yes  | :x:   | yes   | yes    | :x:     | :x:  |
| `getCooperativeVectorMatrixSize`   | :x:     | yes  | :x:   | yes   | yes    | :x:     | :x:  |
| `convertCooperativeVectorMatrix`   | :x:     | yes  | :x:   | yes   | yes    | :x:     | :x:  |
| `isCooperativeMatrixSupported` (3) | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `reportHeaps`                      | yes     | yes  | yes   | yes   | yes    | yes     | yes  |

(1) dummy implementation only
(2) returns nullptr but succeeds
(3) returns false when not supported

## `IBuffer` interface

| API                   | CPU     | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| --------------------- | ------- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getDesc`             | yes     | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSharedHandle`     | :x:     | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `getDeviceAddress`    | yes (1) | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `getDescriptorHandle` | :x:     | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

(1) returns host address

## `ITexture` interface

| API                    | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ---------------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getDesc`              | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSharedHandle`      | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `createView`           | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getDefaultView`       | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSubresourceLayout` | yes | yes  | yes   | yes   | yes    | yes   | yes  |

## `ITextureView` interface

| API                                         | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ------------------------------------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getDesc`                                   | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getTexture`                                | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getDescriptorHandle`                       | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `getCombinedTextureSamplerDescriptorHandle` | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |

## `ISampler` interface

| API                   | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| --------------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getDesc`             | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getDescriptorHandle` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `IFence` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ----------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getCurrentValue` | yes | yes  | :x:   | yes   | yes    | yes   | yes  |
| `setCurrentValue` | yes | yes  | :x:   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `getSharedHandle` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `IShaderObject` interface

| API                         | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| --------------------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getElementTypeLayout`      | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getContainerType`          | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getEntryPointCount`        | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getEntryPoint`             | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `setData`                   | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getObject`                 | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `setObject`                 | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `setBinding`                | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `setDescriptorHandle`       | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `reserveData`               | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `setSpecializationArgs`     | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getRawData`                | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSize`                   | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `setConstantBufferOverride` | :x: | :x:  | :x:   | :x:   | :x:    | :x:   | :x:  |
| `finalize`                  | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `isFinalized`               | yes | yes  | yes   | yes   | yes    | yes   | yes  |

## `ISyntheticShaderProgram` interface

Optional API declared in
[`synthetic-bindings.md`](synthetic-bindings.md) and
`<slang-rhi/synthetic-bindings.h>`.

| API                                | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|------------------------------------|-----|------|-------|-------|--------|-------|------|
| `getSyntheticBindingCount`         | :x: | yes  | :x:   | :x:   | yes    | :x:   | :x:  |
| `getSyntheticBindingLocation`      | :x: | yes  | :x:   | :x:   | yes    | :x:   | :x:  |
| `findSyntheticBindingLocationByID` | :x: | yes  | :x:   | :x:   | yes    | :x:   | :x:  |

## Shader program synthetic resource descriptors

CUDA and Vulkan currently support only `SyntheticResourceScope::Global`.
Entry-point-scoped descriptors return `SLANG_E_NOT_IMPLEMENTED` at program creation.

| Descriptor path                       | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|---------------------------------------|-----|------|-------|-------|--------|-------|------|
| `ShaderProgramSyntheticResourcesDesc` | :x: | yes  | :x:   | :x:   | yes    | :x:   | :x:  |

## Synthetic binding helper

| Helper API                    | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------------------|-----|------|-------|-------|--------|-------|------|
| `bindSyntheticResource(...)`  | :x: | yes  | :x:   | :x:   | yes    | :x:   | :x:  |

## `IShaderTable` interface

## `IPipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ----------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getProgram`      | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | :x: | yes  | yes   | yes   | yes    | yes   | yes  |

## `IRenderPipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ----------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getDesc`         | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `IComputePipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ----------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getDesc`         | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | :x: | yes  | :x:   | yes   | yes    | yes   | yes  |

## `IRayTracingPipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ----------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getDesc`         | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `getNativeHandle` | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |

## `IQueryPool` interface

| API                        | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| -------------------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getDesc`                  | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getResultState`           | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |
| `getResult`                | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |
| `reset`                    | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |
| `reset(queryIndex, count)` | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |

## `ICommandEncoder` interface

| API                                    | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| -------------------------------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getDesc`                              | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `beginRenderPass`                      | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `beginComputePass`                     | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `beginRayTracingPass`                  | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `copyBuffer`                           | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `copyTexture`                          | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `copyTextureToBuffer`                  | :x: | yes  | :x:   | yes   | yes    | yes   | yes  |
| `copyBufferToTexture`                  | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `uploadTextureData`                    | :x: | yes  | :x:   | yes   | yes    | yes   | yes  |
| `uploadBufferData`                     | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `clearBuffer`                          | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `clearTextureFloat`                    | :x: | yes  | yes   | yes   | yes    | yes   | :x:  |
| `clearTextureUint`                     | :x: | yes  | yes   | yes   | yes    | yes   | :x:  |
| `clearTextureSint`                     | :x: | yes  | yes   | yes   | yes    | yes   | :x:  |
| `clearTextureDepthStencil`             | :x: | :x:  | yes   | yes   | yes    | yes   | :x:  |
| `resolveQuery`                         | yes | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `buildAccelerationStructure`           | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `buildMicromap`                        | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `copyAccelerationStructure`            | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `queryAccelerationStructureProperties` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `executeClusterOperation`              | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `convertCooperativeVectorMatrix`       | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `setBufferState`                       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `setTextureState`                      | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `globalBarrier`                        | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `pushDebugGroup`                       | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `popDebugGroup`                        | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `insertDebugMarker`                    | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `writeTimestamp`                       | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |
| `finish`                               | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle`                      | :x: | :x:  | :x:   | :x:   | :x:    | :x:   | :x:  |

## `IPassEncoder` interface

| API                 | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ------------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `pushDebugGroup`    | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `popDebugGroup`     | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `insertDebugMarker` | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `writeTimestamp`    | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `end`               | yes | yes  | yes   | yes   | yes    | yes   | yes  |

## `IRenderPassEncoder` interface

| API                   | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| --------------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `bindPipeline`        | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `setRenderState`      | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `draw`                | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawIndexed`         | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawIndirect`        | :x: | :x:  | yes   | yes   | yes    | :x:   | yes  |
| `drawIndexedIndirect` | :x: | :x:  | yes   | yes   | yes    | :x:   | yes  |
| `drawMeshTasks`       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `IComputePassEncoder` interface

| API                       | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ------------------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `bindPipeline`            | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `dispatchCompute`         | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `dispatchComputeIndirect` | :x: | yes  | yes   | yes   | yes    | yes   | yes  |

## `IRayTracingPassEncoder` interface

| API            | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| -------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `bindPipeline` | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `dispatchRays` | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |

## `ICommandBuffer` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ----------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getDesc`         | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `ICommandQueue` interface

| API                       | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ------------------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getType`                 | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `createCommandEncoder`    | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `submit`                  | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle`         | :x: | yes  | :x:   | yes   | yes    | yes   | yes  |
| `waitOnHost`              | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getTimestampCalibration` | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |

## `ISurface` interface

| API                | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ------------------ | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getInfo`          | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `getConfig`        | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `configure`        | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `unconfigure`      | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `acquireNextImage` | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `present`          | :x: | yes  | yes   | yes   | yes    | yes   | yes  |

Note: CUDA's surface is implemented using a Vulkan swapchain.

If a window change invalidates the swapchain during `acquireNextImage` or `present`, Vulkan
returns `SLANG_E_NOT_AVAILABLE` and `getConfig()` returns null. Acquisition returns no image;
presentation consumes the acquired image without displaying the frame. Release references to
the old images, finish outstanding work, and configure again once the window has a nonzero
drawable size. `configure` also returns `SLANG_E_NOT_AVAILABLE` if the drawable size becomes zero.

## `IAccelerationStructure` interface

| API                   | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| --------------------- | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `getHandle`           | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `getDeviceAddress`    | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `getDescriptorHandle` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `IHeap` interface

| API                | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
| ------------------ | --- | ---- | ----- | ----- | ------ | ----- | ---- |
| `allocate`         | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `free`             | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `report`           | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `flush`            | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `removeEmptyPages` | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |

## OptiX callable stack limits

CUDA ray-tracing pipelines accept `OptixRayTracingPipelineDesc` through
`RayTracingPipelineDesc::next`. The default permits one direct-callable frame from
ray generation, miss or closest-hit. Set `maxDirectCallableDepthFromState` to the
maximum simultaneous `CallShader` nesting depth (two for a callable invoking another
callable). `maxDirectCallableDepthFromTraversal` separately budgets direct calls from
intersection/any-hit and defaults to zero. These limits are independent of
`maxRecursion`, which controls nested ray tracing. Other backends ignore this extension.
The pipeline copies these options, including when compilation is deferred.

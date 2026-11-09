# API implementation status

## `IDevice` interface

| API                                | CPU     | CUDA | D3D11 | D3D12 | Vulkan | Metal   | WGPU |
|------------------------------------|---------|------|-------|-------|--------|---------|------|
| `getNativeDeviceHandles`           | :x:     | yes  | :x:   | yes   | yes    | yes     | :x:  |
| `getInfo`                          | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `hasFeature`                       | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `getFeatures`                      | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `getCapabilities`                  | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `hasCapability`                    | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `getFormatSupport`                 | yes     | yes  | yes   | yes   | yes    | yes (1) | yes  |
| `getSlangSession`                  | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `getQueue`                         | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
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
| `createResourceHeap`               | :x:     | yes  | :x:   | yes   | yes    | yes     | :x:  |
| `getBufferMemoryRequirements`      | :x:     | yes  | :x:   | yes   | yes    | yes     | :x:  |
| `getTextureMemoryRequirements`     | :x:     | :x:  | :x:   | yes   | yes    | yes     | :x:  |
| `isResourceHeapCompatible`         | :x:     | yes  | :x:   | yes   | yes    | yes     | :x:  |
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
|-----------------------|---------|------|-------|-------|--------|-------|------|
| `getDesc`             | yes     | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSharedHandle`     | :x:     | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `getDeviceAddress`    | yes (1) | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `getDescriptorHandle` | :x:     | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

(1) returns host address

## `ITexture` interface

| API                    | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|------------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`              | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSharedHandle`      | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `createView`           | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getDefaultView`       | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSubresourceLayout` | yes | yes  | yes   | yes   | yes    | yes   | yes  |

## `ITextureView` interface

| API                                        | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|--------------------------------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`                                  | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getTexture`                               | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getDescriptorHandle`                      | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `getCombinedTextureSamplerDescriptorHandle`| :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |

## `ISampler` interface

| API                   | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-----------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`             | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getDescriptorHandle` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `IFence` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getCurrentValue` | yes | yes  | :x:   | yes   | yes    | yes   | yes  |
| `setCurrentValue` | yes | yes  | :x:   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `getSharedHandle` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `IShaderObject` interface

| API                         | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-----------------------------|-----|------|-------|-------|--------|-------|------|
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
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getProgram`      | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | :x: | yes  | yes   | yes   | yes    | yes   | yes  |

## `IRenderPipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`         | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `IComputePipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`         | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | :x: | yes  | :x:   | yes   | yes    | yes   | yes  |

## `IRayTracingPipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`         | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `getNativeHandle` | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |

## `IQueryPool` interface

| API                         | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-----------------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`                   | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getResultState`            | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |
| `getResult`                 | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |
| `reset`                     | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |
| `reset(queryIndex, count)`  | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |

## `ICommandEncoder` interface

| API                                    | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|----------------------------------------|-----|------|-------|-------|--------|-------|------|
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
| `buildAccelerationStructures`          | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `buildMicromap`                        | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `copyAccelerationStructure`            | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `queryAccelerationStructureProperties` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `executeClusterOperation`              | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `convertCooperativeVectorMatrix`       | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `setBufferState`                       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `setTextureState`                      | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `globalBarrier`                        | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `aliasResources`                       | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `pushDebugGroup`                       | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `popDebugGroup`                        | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `insertDebugMarker`                    | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `writeTimestamp`                       | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |
| `finish`                               | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle`                      | :x: | :x:  | :x:   | :x:   | :x:    | :x:   | :x:  |

## `IPassEncoder` interface

| API                 | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|---------------------|-----|------|-------|-------|--------|-------|------|
| `pushDebugGroup`    | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `popDebugGroup`     | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `insertDebugMarker` | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `writeTimestamp`    | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `end`               | yes | yes  | yes   | yes   | yes    | yes   | yes  |

## `IRenderPassEncoder` interface

| API                   | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-----------------------|-----|------|-------|-------|--------|-------|------|
| `bindPipeline`        | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `setRenderState`      | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `draw`                | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawIndexed`         | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawIndirect`        | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawIndexedIndirect` | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawMeshTasks`       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

WebGPU single indirect draws without a count buffer use the core WebGPU API.
Other indirect-draw forms currently use Dawn's native multi-draw extension and
require that optional feature to be enabled; they are unavailable in the Wasm path.

Metal indirect draws issue one native indirect draw per argument record and do not
support a count buffer.

See [error handling and diagnostics](error-handling.md#webgpu-recording-and-validation)
for WebGPU recording errors and asynchronous validation.

## `IComputePassEncoder` interface

| API                       | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|---------------------------|-----|------|-------|-------|--------|-------|------|
| `bindPipeline`            | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `dispatchCompute`         | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `dispatchComputeIndirect` | :x: | yes  | yes   | yes   | yes    | yes   | yes  |

## `IRayTracingPassEncoder` interface

| API            | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|----------------|-----|------|-------|-------|--------|-------|------|
| `bindPipeline` | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `dispatchRays` | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |

## `ICommandBuffer` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`         | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `ICommandQueue` interface

| API                       | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|---------------------------|-----|------|-------|-------|--------|-------|------|
| `getType`                 | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `createCommandEncoder`    | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `submit`                  | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle`         | :x: | yes  | :x:   | yes   | yes    | yes   | yes  |
| `waitOnHost`              | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getTimestampCalibration` | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |

## `ISurface` interface

| API                 | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|---------------------|-----|------|-------|-------|--------|-------|------|
| `getInfo`           | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `getConfig`         | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `configure`         | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `unconfigure`       | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `acquireNextImage`  | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `present`           | :x: | yes  | yes   | yes   | yes    | yes   | yes  |

Note: CUDA's surface is implemented using a Vulkan swapchain.

## Acceleration structure buffer placement

`AccelerationStructureDesc::buffer` and `offset` optionally select a fixed storage region;
`size` is its capacity. Null `buffer` with zero `offset` preserves automatic allocation.
D3D12, Vulkan, and CUDA/OptiX support placement. Metal rejects external buffer placement;
CPU, D3D11, and WebGPU do not support acceleration structures.

The buffer must belong to the same device, use `BufferUsage::AccelerationStructure`, and
have `MemoryType::DeviceLocal`. D3D12 also requires the buffer's default state to be
`AccelerationStructureRead` or `AccelerationStructureWrite`. The region must have nonzero capacity and fit inside the
buffer. `IDevice::getInfo().limits.accelerationStructureAlignment` gives the required
buffer offset/address alignment (zero for unsupported backends).
`accelerationStructureScratchAlignment` gives the required scratch device-address
alignment for builds and updates; it may differ from storage alignment, particularly
on Vulkan. These alignments do not require rounding `size` up; round the next region's
offset up when suballocating.

Each acceleration structure retains its backing buffer; destroying it releases that
reference without freeing caller-owned storage. Recorded/submitted commands retain their
acceleration structures and their storage through completion. Callers manage region
reservation and non-overlap, and must not reuse a region until all GPU uses complete,
including traversal through TLAS references. Referenced BLAS objects must remain alive
for those uses. There is no overlap registry or automatic relocation/compaction.
Applications must also synchronize uses across queues as for automatically allocated AS.

D3D12 and Vulkan conservatively synchronize AS operations at command boundaries,
including indirect BLAS references and scratch reuse. Distinct regions can be built,
updated, copied, compacted, and bound through the existing interfaces. CUDA/OptiX uses
the existing stream ordering; its storage address
remains distinct from its traversable handle. As with automatic allocation, bind a CUDA
AS only after its build has executed and produced the handle. CUDA clone copies remain
unsupported; compaction into placed storage is supported.

`buildAccelerationStructures(count, entries)` batches independent builds and updates.
Each `AccelerationStructureBuildEntry` contains the existing single-build arguments.
The single-build API uses a batch of one. Batch boundaries synchronize prior AS uses
and make all results available to later builds, copies, property queries, and traversal.
There is no guaranteed ordering between entries: destination and scratch regions
must be disjoint, and no entry may consume another entry's output, including through
TLAS instance references. In-place updates within one entry are allowed. Shared
read-only inputs are allowed; property queries must use distinct output slots.
Use separate batches for dependent builds or scratch reuse, for example a BLAS batch
followed by a single TLAS build. Cross-queue synchronization remains explicit.

Debug validation checks visible destination/source/scratch interval conflicts and
scratch bounds for multi-build batches. Dependencies hidden in instance data, input
storage aliasing, and aliasing through different buffer resources remain the caller's
responsibility. D3D12 emits the native builds between shared barriers and transitions
buffers between entries when their roles change; Vulkan uses one
native multi-build call. CUDA retains stream ordering, and Metal uses an AS encoder
with fences at the batch boundaries. Descriptors and query arrays are copied during
recording, and referenced resources are retained as for single builds.

## `IAccelerationStructure` interface

| API                   | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-----------------------|-----|------|-------|-------|--------|-------|------|
| `getHandle`           | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `getDeviceAddress`    | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `getDescriptorHandle` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `IHeap` interface

| API                | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|--------------------|-----|------|-------|-------|--------|-------|------|
| `allocate`         | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `free`             | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `report`           | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `flush`            | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `removeEmptyPages` | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |

## `IResourceHeap` interface

See [Resource heaps](resource-heaps.md) for placement, aliasing, lifetime, and synchronization guidance.

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`         | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `getNativeHandle` | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |

## OptiX callable stack limits

CUDA ray-tracing pipelines accept `OptixRayTracingPipelineDesc` through
`RayTracingPipelineDesc::next`. The default permits one direct-callable frame from
ray generation, miss or closest-hit. Set `maxDirectCallableDepthFromState` to the
maximum simultaneous `CallShader` nesting depth (two for a callable invoking another
callable). `maxDirectCallableDepthFromTraversal` separately budgets direct calls from
intersection/any-hit and defaults to zero. These limits are independent of
`maxRecursion`, which controls nested ray tracing. Other backends ignore this extension.
The pipeline copies these options, including when compilation is deferred.

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
| `createTextureFromNativeHandle`    | :x:     | :x:  | :x:   | yes   | :x:    | :x:     | :x:  |
| `createTextureFromSharedHandle`    | :x:     | yes  | :x:   | :x:   | :x:    | :x:     | :x:  |
| `createBuffer`                     | yes     | yes  | yes   | yes   | yes    | yes     | yes  |
| `createBufferFromNativeHandle`     | :x:     | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createBufferFromSharedHandle`     | :x:     | yes  | :x:   | :x:   | :x:    | :x:     | :x:  |
| `mapBuffer`                        | yes     | :x:  | yes   | yes   | yes    | yes     | yes  |
| `unmapBuffer`                      | yes     | :x:  | yes   | yes   | yes    | yes     | yes  |
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
| `getClusterOperationSizes`         | :x:     | yes  | :x:   | yes   | yes    | :x:     | :x:  |
| `createAccelerationStructure`      | :x:     | yes  | :x:   | yes   | yes    | yes     | :x:  |
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

| API         | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`   | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getResult` | yes | yes  | yes   | yes   | yes    | yes   | :x:  |
| `reset`     | yes | yes  | yes   | yes   | yes    | yes   | yes  |

## `ICommandEncoder` interface

| API                                    | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|----------------------------------------|-----|------|-------|-------|--------|-------|------|
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
| `copyAccelerationStructure`            | :x: | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `queryAccelerationStructureProperties` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `serializeAccelerationStructure`       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `deserializeAccelerationStructure`     | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
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
| `drawIndirect`        | :x: | :x:  | yes   | yes   | yes    | :x:   | yes  |
| `drawIndexedIndirect` | :x: | :x:  | yes   | yes   | yes    | :x:   | yes  |
| `drawMeshTasks`       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `IComputePassEncoder` interface

| API                       | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|---------------------------|-----|------|-------|-------|--------|-------|------|
| `bindPipeline`            | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `dispatchCompute`         | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `dispatchComputeIndirect` | :x: | yes  | yes   | yes   | yes    | :x:   | yes  |

## `IRayTracingPassEncoder` interface

| API            | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|----------------|-----|------|-------|-------|--------|-------|------|
| `bindPipeline` | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `dispatchRays` | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |

## `ICommandBuffer` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `ICommandQueue` interface

| API                     | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------------|-----|------|-------|-------|--------|-------|------|
| `getType`               | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `createCommandEncoder`  | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `submit`                | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle`       | :x: | yes  | :x:   | yes   | yes    | yes   | yes  |
| `waitOnHost`            | yes | yes  | yes   | yes   | yes    | yes   | yes  |

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

# API implementation status

## `IDevice` interface

| API                                       | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal   | WGPU |
|-------------------------------------------|-----|------|-------|-------|--------|---------|------|
| `getNativeDeviceHandles`                  | :x: | yes  | :x:   | yes   | yes    | yes     | :x:  |
| `hasFeature`                              | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `getFeatures`                             | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `getSlangSession`                         | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `getFormatSupport`                        | :x: | :x:  | :x:   | yes   | yes    | yes (1) | :x:  |
| `createTexture`                           | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createTextureFromNativeHandle`           | :x: | :x:  | :x:   | yes   | :x:    | :x:     | :x:  |
| `createBuffer`                            | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createBufferFromNativeHandle`            | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createBufferFromSharedHandle`            | :x: | yes  | :x:   | :x:   | :x:    | :x:     | :x:  |
| `createSampler`                           | :x: | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createTextureView`                       | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createSwapchain`                         | :x: | :x:  | yes   | yes   | yes    | yes     | :x:  |
| `createInputLayout`                       | :x: | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createCommandQueue`                      | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createShaderObject`                      | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createShaderObject2`                     | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createShaderObjectFromTypeLayout`        | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createMutableShaderObject`               | yes | yes  | yes   | yes   | yes    | :x:     | yes  |
| `createMutableShaderObject2`              | yes | yes  | yes   | yes   | yes    | :x:     | yes  |
| `createMutableShaderObjectFromTypeLayout` | yes | yes  | yes   | yes   | yes    | :x:     | yes  |
| `createMutableRootShaderObject`           | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createShaderTable`                       | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createShaderProgram`                     | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createRenderPipeline`                    | :x: | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createComputePipeline`                   | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createRayTracingPipeline`                | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `readTexture`                             | :x: | yes  | yes   | yes   | yes    | yes     | yes  |
| `readBuffer`                              | :x: | yes  | yes   | yes   | yes    | yes     | yes  |
| `createQueryPool`                         | yes | yes  | yes   | yes   | yes    | yes     | :x:  |
| `getAccelerationStructurePrebuildInfo`    | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createAccelerationStructure`             | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createFence`                             | :x: | :x:  | :x:   | yes   | yes    | yes     | :x:  |
| `waitForFences`                           | :x: | :x:  | :x:   | yes   | yes    | yes     | :x:  |
| `getTextureAllocationInfo`                | :x: | :x:  | :x:   | yes   | yes    | yes     | :x:  |
| `getTextureRowAlignment`                  | :x: | :x:  | :x:   | yes   | yes    | yes     | :x:  |

(1) dummy implementation only

## `IBuffer` interface

| API                | CPU     | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|--------------------|---------|------|-------|-------|--------|-------|------|
| `getDesc`          | yes     | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSharedHandle`  | :x:     | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `getDeviceAddress` | yes (1) | yes  | :x:   | yes   | yes    | yes   | :x:  |
| `map`              | yes     | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `unmap`            | yes     | :x:  | :x:   | yes   | yes    | yes   | yes  |

(1) returns host address

## `ITexture` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`         | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSharedHandle` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `ITextureView` interface

## `IFence` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getCurrentValue` | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `setCurrentValue` | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `getSharedHandle` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `IShaderObject` interface

## `IShaderTable` interface

## `IPipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `IQueryPool` interface

| API         | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------|-----|------|-------|-------|--------|-------|------|
| `getResult` | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |
| `reset`     | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |

## `ICommandEncoder` interface

| API                          | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|------------------------------|-----|------|-------|-------|--------|-------|------|
| `endEncoding`                | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `setBufferState`             | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `setTextureState`            | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `setTextureSubresourceState` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `beginDebugEvent`            | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `endDebugEvent`              | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `writeTimestamp`             | yes | yes  | yes   | yes   | yes    | yes   | :x:  |

## `IResourceCommandEncoder` interface

| API                   | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-----------------------|-----|------|-------|-------|--------|-------|------|
| `copyBuffer`          | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `copyTexture`         | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `copyTextureToBuffer` | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `uploadTextureData`   | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `uploadBufferData`    | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `clearBuffer`         | :x: | :x:  | :x:   | :x:   | yes    | :x:   | yes  |
| `clearTexture`        | :x: | :x:  | :x:   | :x:   | :x:    | :x:   | :x:  |
| `resolveQuery`        | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |

## `IRenderCommandEncoder` interface

| API                          | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|------------------------------|-----|------|-------|-------|--------|-------|------|
| `bindPipeline`               | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `bindPipelineWithRootObject` | :x: | :x:  | yes   | yes   | yes    | :x:   | yes  |
| `setViewports`               | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `setScissorRects`            | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `setVertexBuffers`           | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `setIndexBuffer`             | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `setSamplePositions`         | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `setStencilReference`        | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `draw`                       | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawIndirect`               | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `drawIndexed`                | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawIndexedIndirect`        | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `drawInstanced`              | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawIndexedInstanced`       | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawMeshTasks`              | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `IComputeCommandEncoder` interface

| API                          | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|------------------------------|-----|------|-------|-------|--------|-------|------|
| `bindPipeline`               | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `bindPipelineWithRootObject` | yes | yes  | yes   | yes   | yes    | :x:   | yes  |
| `dispatchCompute`            | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `dispatchComputeIndirect`    | :x: | :x:  | :x:   | yes   | :x:    | :x:   | yes  |

## `IRayTracingCommandEncoder` interface

| API                                    | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|----------------------------------------|-----|------|-------|-------|--------|-------|------|
| `buildAccelerationStructure`           | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `copyAccelerationStructure`            | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `queryAccelerationStructureProperties` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `serializeAccelerationStructure`       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `deserializeAccelerationStructure`     | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `bindPipeline`                         | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `bindPipelineWithRootObject`           | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `dispatchRays`                         | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `ICommandBuffer` interface

| API                        | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|----------------------------|-----|------|-------|-------|--------|-------|------|
| `encodeResourceCommands`   | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `encodeRenderCommands`     | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `encodeComputeCommands`    | yes | yes  | yes   | yes   | yes    | :x:   | yes  |
| `encodeRayTracingCommands` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `close`                    | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle`          | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `ICommandQueue` interface

| API                          | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|------------------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`                    | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `executeCommandBuffers`      | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle`            | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `waitOnHost`                 | yes | yes  | yes   | yes   | yes    | :x:   | yes  |
| `waitForFenceValuesOnDevice` | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |

## `ISwapchain` interface

| API                 | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|---------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`           | :x: | :x:  | yes   | yes   | yes    | yes   | :x:  |
| `getImage`          | :x: | :x:  | yes   | yes   | yes    | yes   | :x:  |
| `present`           | :x: | :x:  | yes   | yes   | yes    | yes   | :x:  |
| `acquireNextImage`  | :x: | :x:  | yes   | yes   | yes    | yes   | :x:  |
| `resize`            | :x: | :x:  | yes   | yes   | yes    | yes   | :x:  |
| `isOccluded`        | :x: | :x:  | :x:   | yes   | :x:    | :x:   | :x:  |
| `setFullScreenMode` | :x: | :x:  | :x:   | yes   | :x:    | :x:   | :x:  |




| API           | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|---------------|-----|------|-------|-------|--------|-------|------|
| `endEncoding` | :x: | :x:  | :x:   | :x:   | :x:    | :x:   | :x:  |

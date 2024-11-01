# API implementation status

## `IDevice` interface

| API                                       | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal   | WGPU |
|-------------------------------------------|-----|------|-------|-------|--------|---------|------|
| `getNativeDeviceHandles`                  | :x: | yes  | :x:   | yes   | yes    | yes     | :x:  |
| `hasFeature`                              | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `getFeatures`                             | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `getSlangSession`                         | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `getFormatSupport`                        | :x: | :x:  | :x:   | yes   | yes    | yes (1) | :x:  |
| `getQueue`                                | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createTexture`                           | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createTextureFromNativeHandle`           | :x: | :x:  | :x:   | yes   | :x:    | :x:     | :x:  |
| `createBuffer`                            | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createBufferFromNativeHandle`            | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createBufferFromSharedHandle`            | :x: | yes  | :x:   | :x:   | :x:    | :x:     | :x:  |
| `createSampler`                           | :x: | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createTextureView`                       | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createSwapchain`                         | :x: | :x:  | yes   | yes   | yes    | yes     | :x:  |
| `createInputLayout`                       | :x: | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createShaderObject`                      | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createShaderObjectFromTypeLayout`        | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createShaderTable`                       | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createShaderProgram`                     | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createRenderPipeline`                    | :x: | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createComputePipeline`                   | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createRayTracingPipeline`                | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `readTexture`                             | :x: | yes  | yes   | yes   | yes    | yes     | yes  |
| `readBuffer`                              | :x: | yes  | yes   | yes   | yes    | yes     | yes  |
| `createQueryPool`                         | yes | yes  | yes   | yes   | yes    | yes     | :x:  |
| `getAccelerationStructureSizes`           | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
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

## `IRenderPipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `IComputePipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `IRayTracingPipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |

## `IQueryPool` interface

| API         | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------|-----|------|-------|-------|--------|-------|------|
| `getResult` | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |
| `reset`     | yes | yes  | yes   | yes   | yes    | :x:   | :x:  |

## `ICommandEncoder` interface

| API                                    | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|----------------------------------------|-----|------|-------|-------|--------|-------|------|
| `copyBuffer`                           | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `copyTexture`                          | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `copyTextureToBuffer`                  | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `uploadTextureData`                    | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `uploadBufferData`                     | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `clearBuffer`                          | :x: | :x:  | :x:   | :x:   | yes    | :x:   | yes  |
| `clearTexture`                         | :x: | :x:  | :x:   | :x:   | :x:    | :x:   | :x:  |
| `resolveQuery`                         | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `beginRenderPass`                      | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `endRenderPass`                        | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `setRenderState`                       | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `draw`                                 | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawIndexed`                          | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `drawIndirect`                         | :x: | :x:  | yes   | yes   | yes    | :x:   | yes  |
| `drawIndexedIndirect`                  | :x: | :x:  | yes   | yes   | yes    | :x:   | yes  |
| `drawMeshTasks`                        | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `setComputeState`                      | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `dispatchCompute`                      | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `dispatchComputeIndirect`              | :x: | :x:  | yes   | yes   | :x:    | :x:   | yes  |
| `setRayTracingState`                   | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `dispatchRays`                         | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `buildAccelerationStructure`           | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `copyAccelerationStructure`            | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `queryAccelerationStructureProperties` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `serializeAccelerationStructure`       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `deserializeAccelerationStructure`     | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `setBufferState`                       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `setTextureState`                      | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `setTextureSubresourceState`           | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `beginDebugEvent`                      | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `endDebugEvent`                        | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `writeTimestamp`                       | yes | yes  | yes   | yes   | yes    | yes   | :x:  |
| `finish`                               | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle`                      | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `ICommandBuffer` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getNativeHandle` | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `ICommandQueue` interface

| API                          | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|------------------------------|-----|------|-------|-------|--------|-------|------|
| `getType`                    | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `createCommanEncoder`        | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `submit`                     | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle`            | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `waitOnHost`                 | yes | yes  | yes   | yes   | yes    | :x:   | yes  |
| `waitForFenceValuesOnDevice` | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |

## `ISurface` interface

| API                 | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|---------------------|-----|------|-------|-------|--------|-------|------|
| `getInfo`           | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `getConfig`         | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `configure`         | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `getCurrentTexture` | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `present`           | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |

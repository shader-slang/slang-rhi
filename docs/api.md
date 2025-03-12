# API implementation status

## `IDevice` interface

| API                                | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal   | WGPU |
|------------------------------------|-----|------|-------|-------|--------|---------|------|
| `getNativeDeviceHandles`           | :x: | yes  | :x:   | yes   | yes    | yes     | :x:  |
| `getDeviceInfo`                    | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `hasFeature`                       | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `getFeatures`                      | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `getFormatSupport`                 | :x: | :x:  | :x:   | yes   | yes    | yes (1) | :x:  |
| `getSlangSession`                  | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `getQueue`                         | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createTexture`                    | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createTextureFromNativeHandle`    | :x: | :x:  | :x:   | yes   | :x:    | :x:     | :x:  |
| `createBuffer`                     | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createBufferFromNativeHandle`     | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createBufferFromSharedHandle`     | :x: | yes  | :x:   | :x:   | :x:    | :x:     | :x:  |
| `mapBuffer`                        | yes | :x:  | yes   | yes   | yes    | yes     | yes  |
| `unmapBuffer`                      | yes | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createSampler`                    | :x: | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createTextureView`                | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createSurface`                    | :x: | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createInputLayout`                | :x: | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createShaderObject`               | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createShaderObjectFromTypeLayout` | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createRootShaderObject`           | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createShaderTable`                | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createShaderProgram`              | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createRenderPipeline`             | :x: | :x:  | yes   | yes   | yes    | yes     | yes  |
| `createComputePipeline`            | yes | yes  | yes   | yes   | yes    | yes     | yes  |
| `createRayTracingPipeline`         | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `readTexture`                      | :x: | yes  | yes   | yes   | yes    | yes     | yes  |
| `readBuffer`                       | :x: | yes  | yes   | yes   | yes    | yes     | yes  |
| `createQueryPool`                  | yes | yes  | yes   | yes   | yes    | yes     | :x:  |
| `getAccelerationStructureSizes`    | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createAccelerationStructure`      | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `createFence`                      | :x: | :x:  | :x:   | yes   | yes    | yes     | :x:  |
| `waitForFences`                    | :x: | :x:  | :x:   | yes   | yes    | yes     | :x:  |
| `getTextureAllocationInfo`         | :x: | :x:  | :x:   | yes   | yes    | yes     | :x:  |
| `getTextureRowAlignment`           | :x: | :x:  | :x:   | yes   | yes    | yes     | :x:  |
| `getCooperativeVectorProperties`   | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |
| `convertCooperativeVectorMatrix`   | :x: | :x:  | :x:   | yes   | yes    | :x:     | :x:  |

(1) dummy implementation only

## `IBuffer` interface

| API                | CPU     | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|--------------------|---------|------|-------|-------|--------|-------|------|
| `getDesc`          | yes     | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSharedHandle`  | :x:     | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `getDeviceAddress` | yes (1) | yes  | :x:   | yes   | yes    | yes   | :x:  |

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
| `beginRenderPass`                      | :x: | :x:  | yes   | yes   | yes    | yes   | yes  |
| `beginComputePass`                     | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `beginRayTracingPass`                  | :x: | yes  | :x:   | yes   | yes    | :x:   | :x:  |
| `copyBuffer`                           | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `copyTexture`                          | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `copyTextureToBuffer`                  | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `uploadTextureData`                    | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `uploadBufferData`                     | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `clearBuffer`                          | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `clearTextureFloat`                    | :x: | :x:  | :x:   | :x:   | :x:    | :x:   | :x:  |
| `clearTextureUint`                     | :x: | :x:  | :x:   | :x:   | :x:    | :x:   | :x:  |
| `clearTextureDepthStencil`             | :x: | :x:  | yes   | yes   | yes    | :x:   | :x:  |
| `resolveQuery`                         | :x: | :x:  | :x:   | yes   | yes    | yes   | :x:  |
| `buildAccelerationStructure`           | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `copyAccelerationStructure`            | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `queryAccelerationStructureProperties` | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `serializeAccelerationStructure`       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `deserializeAccelerationStructure`     | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `convertCooperativeVectorMatrix`       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `setBufferState`                       | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `setTextureState`                      | :x: | :x:  | :x:   | yes   | yes    | :x:   | :x:  |
| `pushDebugGroup`                       | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `popDebugGroup`                        | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `insertDebugMarker`                    | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `writeTimestamp`                       | yes | yes  | yes   | yes   | yes    | yes   | :x:  |
| `finish`                               | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle`                      | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |

## `IPassEncoder` interface

| API                 | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|---------------------|-----|------|-------|-------|--------|-------|------|
| `pushDebugGroup`    | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `popDebugGroup`     | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
| `insertDebugMarker` | :x: | :x:  | :x:   | yes   | yes    | yes   | yes  |
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
| `dispatchComputeIndirect` | :x: | :x:  | yes   | yes   | yes    | :x:   | yes  |

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
| `getInfo`           | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `getConfig`         | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `configure`         | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `getCurrentTexture` | :x: | yes  | yes   | yes   | yes    | yes   | yes  |
| `present`           | :x: | yes  | yes   | yes   | yes    | yes   | yes  |

Note: CUDA's surface is implemented using a Vulkan swapchain.

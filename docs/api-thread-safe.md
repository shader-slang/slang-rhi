# API thread-safe status

## `IDevice` interface

| API                                | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|------------------------------------|-----|------|-------|-------|--------|-------|------|
| `getNativeDeviceHandles`           | n/a | yes  | n/a   | yes   | yes    | yes   | n/a  |
| `getInfo`                          | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `hasFeature`                       | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getFeatures`                      | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getCapabilities`                  | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `hasCapability`                    | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getFormatSupport`                 | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSlangSession`                  | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getQueue`                         | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `createTexture`                    | no  | no   | no    | no    | no     | no    | no   |
| `createTextureFromNativeHandle`    | n/a | n/a  | n/a   | no    | n/a    | n/a   | n/a  |
| `createTextureFromSharedHandle`    | n/a | no   | n/a   | n/a   | n/a    | n/a   | n/a  |
| `createBuffer`                     | no  | no   | no    | no    | no     | no    | no   |
| `createBufferFromNativeHandle`     | n/a | n/a  | n/a   | no    | no     | n/a   | n/a  |
| `createBufferFromSharedHandle`     | n/a | no   | n/a   | n/a   | n/a    | n/a   | n/a  |
| `mapBuffer`                        | no  | n/a  | no    | no    | no     | no    | no   |
| `unmapBuffer`                      | no  | n/a  | no    | no    | no     | no    | no   |
| `createSampler`                    | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `createTextureView`                | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `createSurface`                    | n/a | no   | no    | no    | no     | no    | no   |
| `createInputLayout`                | n/a | n/a  | yes   | yes   | yes    | yes   | yes  |
| `createShaderObject`               | no  | no   | no    | no    | no     | no    | no   |
| `createShaderObjectFromTypeLayout` | no  | no   | no    | no    | no     | no    | no   |
| `createRootShaderObject`           | no  | no   | no    | no    | no     | no    | no   |
| `createShaderTable`                | n/a | no   | n/a   | no    | no     | n/a   | n/a  |
| `createShaderProgram`              | no  | no   | no    | no    | no     | no    | no   |
| `createRenderPipeline`             | n/a | n/a  | no    | no    | no     | no    | no   |
| `createComputePipeline`            | no  | no   | no    | no    | no     | no    | no   |
| `createRayTracingPipeline`         | n/a | no   | n/a   | no    | no     | n/a   | n/a  |
| `readTexture`                      | no  | no   | no    | no    | no     | no    | no   |
| `readBuffer`                       | no  | no   | no    | no    | no     | no    | no   |
| `createQueryPool`                  | no  | no   | no    | no    | no     | no    | no   |
| `getAccelerationStructureSizes`    | n/a | yes  | n/a   | yes   | yes    | yes   | n/a  |
| `getClusterOperationSizes`         | n/a | yes  | n/a   | yes   | yes    | n/a   | n/a  |
| `createAccelerationStructure`      | n/a | yes  | n/a   | no    | no     | no    | n/a  |
| `createFence`                      | no  | no   | n/a   | no    | no     | no    | no   |
| `waitForFences`                    | no  | no   | n/a   | no    | no     | no    | no   |
| `createHeap`                       | n/a | no   | n/a   | no    | no     | n/a   | n/a  |
| `getTextureAllocationInfo`         | yes | yes  | n/a   | yes   | yes    | yes   | n/a  |
| `getTextureRowAlignment`           | yes | yes  | n/a   | yes   | yes    | yes   | yes  |
| `getCooperativeVectorProperties`   | n/a | yes  | n/a   | yes   | yes    | n/a   | n/a  |
| `getCooperativeVectorMatrixSize`   | n/a | yes  | n/a   | yes   | yes    | n/a   | n/a  |
| `convertCooperativeVectorMatrix`   | n/a | yes  | n/a   | yes   | yes    | n/a   | n/a  |
| `reportHeaps`                      | no  | no   | no    | no    | no     | no    | no   |

## `IBuffer` interface

| API                   | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-----------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`             | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSharedHandle`     | n/a | n/a  | n/a   | yes   | yes    | n/a   | n/a  |
| `getDeviceAddress`    | yes | yes  | n/a   | yes   | yes    | yes   | n/a  |
| `getDescriptorHandle` | n/a | n/a  | n/a   | yes   | yes    | n/a   | n/a  |

## `ITexture` interface

| API                    | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|------------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`              | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSharedHandle`      | n/a | n/a  | n/a   | yes   | yes    | n/a   | n/a  |
| `createView`           | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getDefaultView`       | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getSubresourceLayout` | yes | yes  | yes   | yes   | yes    | yes   | yes  |

## `ITextureView` interface

| API                                        | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|--------------------------------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`                                  | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getTexture`                               | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getDescriptorHandle`                      | n/a | yes  | n/a   | yes   | yes    | n/a   | n/a  |
| `getCombinedTextureSamplerDescriptorHandle`| n/a | yes  | n/a   | yes   | yes    | n/a   | n/a  |

## `ISampler` interface

| API                   | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-----------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`             | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getDescriptorHandle` | n/a | n/a  | n/a   | yes   | yes    | n/a   | n/a  |

## `IFence` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getCurrentValue` | no  | no   | n/a   | no    | no     | no    | no   |
| `setCurrentValue` | no  | no   | n/a   | no    | no     | no    | no   |
| `getNativeHandle` | n/a | n/a  | n/a   | no    | no     | no    | n/a  |
| `getSharedHandle` | n/a | n/a  | n/a   | no    | no     | n/a   | n/a  |

## `IShaderObject` interface

Not thread-safe. All operations must be synchronized externally.

## `IShaderTable` interface

## `IPipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getProgram`      | no  | no   | no    | no    | no     | no    | no   |
| `getNativeHandle` | n/a | no   | no    | no    | no     | no    | no   |

## `IRenderPipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`         | n/a | n/a  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | n/a | n/a  | n/a   | yes   | yes    | yes   | yes  |

## `IComputePipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`         | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getNativeHandle` | n/a | yes  | n/a   | yes   | yes    | yes   | yes  |

## `IRayTracingPipeline` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`         | n/a | yes  | n/a   | yes   | yes    | n/a   | n/a  |
| `getNativeHandle` | n/a | yes  | n/a   | yes   | yes    | n/a   | n/a  |

## `IQueryPool` interface

| API         | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------|-----|------|-------|-------|--------|-------|------|
| `getDesc`   | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `getResult` | no  | no   | no    | no    | no     | no    | n/a  |
| `reset`     | no  | no   | no    | no    | no     | no    | no   |

## `ICommandEncoder` interface

Not thread-safe. All operations must be synchronized externally.

## `IPassEncoder` interface

Not thread-safe. All operations must be synchronized externally.

## `IRenderPassEncoder` interface

Not thread-safe. All operations must be synchronized externally.

## `IComputePassEncoder` interface

Not thread-safe. All operations must be synchronized externally.

## `IRayTracingPassEncoder` interface

Not thread-safe. All operations must be synchronized externally.

## `ICommandBuffer` interface

| API               | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------|-----|------|-------|-------|--------|-------|------|
| `getNativeHandle` | n/a | n/a  | n/a   | yes   | yes    | yes   | yes  |

## `ICommandQueue` interface

| API                     | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-------------------------|-----|------|-------|-------|--------|-------|------|
| `getType`               | yes | yes  | yes   | yes   | yes    | yes   | yes  |
| `createCommandEncoder`  | no  | no   | no    | no    | no     | no    | no   |
| `submit`                | no  | no   | no    | no    | no     | no    | no   |
| `getNativeHandle`       | n/a | yes  | n/a   | yes   | yes    | yes   | yes  |
| `waitOnHost`            | no  | no   | no    | no    | no     | no    | no   |

## `ISurface` interface

| API                 | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|---------------------|-----|------|-------|-------|--------|-------|------|
| `getInfo`           | n/a | yes  | yes   | yes   | yes    | yes   | yes  |
| `getConfig`         | n/a | no   | no    | no    | no     | no    | no   |
| `configure`         | n/a | no   | no    | no    | no     | no    | no   |
| `unconfigure`       | n/a | no   | no    | no    | no     | no    | no   |
| `acquireNextImage`  | n/a | no   | no    | no    | no     | no    | no   |
| `present`           | n/a | no   | no    | no    | no     | no    | no   |

## `IAccelerationStructure` interface

| API                   | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|-----------------------|-----|------|-------|-------|--------|-------|------|
| `getHandle`           | n/a | yes  | n/a   | yes   | yes    | yes   | n/a  |
| `getDeviceAddress`    | n/a | yes  | n/a   | yes   | yes    | yes   | n/a  |
| `getDescriptorHandle` | n/a | n/a  | n/a   | yes   | yes    | n/a   | n/a  |

## `IHeap` interface

| API                | CPU | CUDA | D3D11 | D3D12 | Vulkan | Metal | WGPU |
|--------------------|-----|------|-------|-------|--------|-------|------|
| `allocate`         | n/a | no   | n/a   | no    | no     | n/a   | n/a  |
| `free`             | n/a | no   | n/a   | no    | no     | n/a   | n/a  |
| `report`           | n/a | no   | n/a   | no    | no     | n/a   | n/a  |
| `flush`            | n/a | no   | n/a   | no    | no     | n/a   | n/a  |
| `removeEmptyPages` | n/a | no   | n/a   | no    | no     | n/a   | n/a  |

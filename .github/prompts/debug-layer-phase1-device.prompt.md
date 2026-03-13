## Phase 1: DebugDevice Validation — [debug-device.cpp](src/debug-layer/debug-device.cpp)

Add parameter validation to `DebugDevice` methods that currently only do label patching or pure forwarding. Each step below is one method to update.

### Context

- The debug layer wraps backend implementations, performs validation, then forwards to the inner object via `baseObject->method(...)`.
- Use `RHI_VALIDATION_ERROR(...)` for invalid usage that will likely crash or produce wrong results. Return `SLANG_E_INVALID_ARG` after the error.
- Use `RHI_VALIDATION_WARNING(...)` for suspicious but technically allowed usage.
- Look at the existing `createTexture` and `createSampler` validation in `debug-device.cpp` as style reference.
- The `SLANG_RHI_API_FUNC` macro is used at the top of each method.
- **Documentation**: After implementing validation for each step, update the corresponding method's doc comment in [slang-rhi.h](include/slang-rhi.h) to document parameter constraints, valid usage rules, and error conditions (e.g., `@param desc ... size must be > 0`, `@returns SLANG_E_INVALID_ARG if ...`). Use the validation checks as the source of truth for what to document.

### Steps

#### Step 1: `createBuffer`
Currently only patches the label. Add validation:
- `desc.size == 0` → `RHI_VALIDATION_ERROR("Buffer size must be greater than 0")`
- `desc.usage == BufferUsage::None` → `RHI_VALIDATION_ERROR("Buffer usage must be specified")`
- `desc.elementSize > 0 && desc.size % desc.elementSize != 0` → `RHI_VALIDATION_WARNING("Buffer size is not a multiple of element size")`
- `outBuffer == nullptr` → `RHI_VALIDATION_ERROR("outBuffer is null")`

#### Step 2: `createTextureView`
Currently only patches the label. Add validation:
- `desc.texture == nullptr` → `RHI_VALIDATION_ERROR("Texture view requires a non-null texture")`
- Resolve the subresource range against the texture desc, then validate:
  - `subresourceRange.mip + subresourceRange.mipCount > texture.mipCount` → error
  - `subresourceRange.layer + subresourceRange.layerCount > texture.arrayLength` → error
  - Use `kRemainingMipLevels`/`kRemainingArrayLayers` sentinel values (check how `resolveSubresourceRange` handles them)
- `outView == nullptr` → `RHI_VALIDATION_ERROR("outView is null")`

#### Step 3: `createShaderProgram`
Currently only patches the label and calls `validateCudaContext`. Add validation:
- `desc.slangGlobalScope == nullptr && desc.slangEntryPointCount == 0` → `RHI_VALIDATION_ERROR("Shader program requires at least a global scope or entry point")`
- `desc.slangEntryPointCount > 0 && desc.slangEntryPoints == nullptr` → `RHI_VALIDATION_ERROR("slangEntryPoints is null but slangEntryPointCount > 0")`
- `outProgram == nullptr` → `RHI_VALIDATION_ERROR("outProgram is null")`

#### Step 4: `createAccelerationStructure`
Currently only patches the label. Add validation:
- `desc.size == 0` → `RHI_VALIDATION_ERROR("Acceleration structure size must be greater than 0")`
- `outAccelerationStructure == nullptr` → `RHI_VALIDATION_ERROR("outAccelerationStructure is null")`

#### Step 5: `createInputLayout`
Currently a pure forward. Add validation:
- `desc.inputElementCount > 0 && desc.inputElements == nullptr` → `RHI_VALIDATION_ERROR("inputElements is null but inputElementCount > 0")`
- `desc.vertexStreamCount > 0 && desc.vertexStreams == nullptr` → `RHI_VALIDATION_ERROR("vertexStreams is null but vertexStreamCount > 0")`
- For each input element: `element.bufferSlotIndex >= desc.vertexStreamCount` → `RHI_VALIDATION_ERROR("Input element bufferSlotIndex out of range")`
- `outLayout == nullptr` → `RHI_VALIDATION_ERROR("outLayout is null")`

#### Step 6: `createShaderTable`
Currently a pure forward. Add validation:
- `desc.program == nullptr` → `RHI_VALIDATION_ERROR("Shader table requires a non-null program")`
- `desc.rayGenShaderCount > 0 && desc.rayGenShaderEntryPointNames == nullptr` → error
- `desc.missShaderCount > 0 && desc.missShaderEntryPointNames == nullptr` → error
- `desc.hitGroupCount > 0 && desc.hitGroupNames == nullptr` → error
- `desc.callableShaderCount > 0 && desc.callableShaderEntryPointNames == nullptr` → error
- `outShaderTable == nullptr` → `RHI_VALIDATION_ERROR("outShaderTable is null")`

#### Step 7: `createQueryPool`
Currently only wraps and patches label. Add validation:
- `desc.count == 0` → `RHI_VALIDATION_ERROR("Query pool count must be greater than 0")`
- `outPool == nullptr` → `RHI_VALIDATION_ERROR("outPool is null")`

#### Step 8: `createHeap`
Currently only wraps and patches label. Add validation:
- `desc.usage == HeapUsage::None` → `RHI_VALIDATION_ERROR("Heap usage must be specified")`
- `outHeap == nullptr` → `RHI_VALIDATION_ERROR("outHeap is null")`

#### Step 9: `mapBuffer` / `unmapBuffer`
`mapBuffer` already validates `CpuAccessMode` vs `MemoryType`. Add:
- `buffer == nullptr` → error in both `mapBuffer` and `unmapBuffer`
- `outData == nullptr` → error in `mapBuffer`
- Consider adding per-buffer mapped-state tracking in `DebugDevice` (e.g., `std::unordered_set<IBuffer*> m_mappedBuffers`):
  - `mapBuffer`: if buffer already in set → `RHI_VALIDATION_ERROR("Buffer is already mapped")`
  - `unmapBuffer`: if buffer not in set → `RHI_VALIDATION_ERROR("Buffer is not mapped")`

#### Step 10: `readBuffer`
Currently only calls `validateCudaContext`. Add:
- `buffer == nullptr` → error
- Validate offset + size against `buffer->getDesc().size`
- For the blob overload: `outBlob == nullptr` → error
- For the data pointer overload: `outData == nullptr` → error

#### Step 11: Null output pointer checks for `createBufferFromNativeHandle`, `createBufferFromSharedHandle`, `createTextureFromNativeHandle`, `createTextureFromSharedHandle`
Low priority. Add null output pointer checks to each.

#### Step 12: `createFence`
Low priority. Add:
- `outFence == nullptr` → error

### Verification

- Build: `cmake --build ./build --config Debug`
- Run tests: `./build/Debug/slang-rhi-tests.exe` — all existing tests must pass
- Ensure new validation only fires on genuinely invalid input, not on anything the test suite exercises
- Run `pre-commit run --all-files` to fix formatting

## Phase 2: DebugCommandEncoder Validation — [debug-command-encoder.cpp](src/debug-layer/debug-command-encoder.cpp)

Add parameter and range validation to `DebugCommandEncoder` methods that currently only do `requireOpen()` + `requireNoPass()` state checks.

### Context

- The debug layer's `DebugCommandEncoder` wraps `ICommandEncoder`. All methods already call `requireOpen()` and `requireNoPass()` for state tracking.
- Use `RHI_VALIDATION_ERROR(...)` and return `SLANG_E_INVALID_ARG` for invalid usage. Use `RHI_VALIDATION_WARNING(...)` for suspicious usage.
- Look at the existing `copyTexture`, `clearBuffer`, and `clearTextureDepthStencil` validation as style reference.
- The inner encoder is accessed via `getBaseEncoder()`.
- **Documentation**: After implementing validation for each step, update the corresponding method's doc comment in [slang-rhi.h](include/slang-rhi.h) to document parameter constraints, valid usage rules, and error conditions. Use the validation checks as the source of truth for what to document.

### Steps

#### Step 1: `copyBuffer`
Currently only does state checks. Add:
- `dst == nullptr` → `RHI_VALIDATION_ERROR("copyBuffer: dst is null")`
- `src == nullptr` → `RHI_VALIDATION_ERROR("copyBuffer: src is null")`
- `size == 0` → `RHI_VALIDATION_WARNING("copyBuffer: size is 0")`
- `srcOffset + size > src->getDesc().size` → `RHI_VALIDATION_ERROR("copyBuffer: source range out of bounds")`
- `dstOffset + size > dst->getDesc().size` → `RHI_VALIDATION_ERROR("copyBuffer: destination range out of bounds")`
- If `dst == src`, check for overlap: ranges `[srcOffset, srcOffset+size)` and `[dstOffset, dstOffset+size)` overlap → `RHI_VALIDATION_WARNING("copyBuffer: overlapping source and destination ranges on same buffer")`
- Verify `src` has `CopySource` usage flag, `dst` has `CopyDestination` usage flag → warnings

#### Step 2: `uploadBufferData`
Currently only does state checks. Add:
- `dst == nullptr` → error
- `data == nullptr` → error
- `size == 0` → warning
- `offset + size > dst->getDesc().size` → error

#### Step 3: `clearTextureFloat`
Currently only does state checks. Add:
- `texture == nullptr` → error
- Validate subresource range bounds against texture desc (`mip + mipCount <= mipLevels`, `layer + layerCount <= arrayLength`)
- Check texture has `RenderTarget` or `UnorderedAccess` usage → error if neither
- Check format is float-compatible (not a uint-only or sint-only format) → warning

#### Step 4: `clearTextureUint`
Same as Step 3, but check format is uint-compatible.

#### Step 5: `clearTextureSint`
Same as Step 3, but check format is sint-compatible.

#### Step 6: `resolveQuery`
Currently only does state checks. Add:
- `queryPool == nullptr` → error
- `buffer == nullptr` → error
- `count == 0` → warning
- `index + count > queryPool->getDesc().count` → error
- `offset + count * sizeof(uint64_t) > buffer->getDesc().size` → error

#### Step 7: `copyAccelerationStructure`
Currently only does state checks. Add:
- `dst == nullptr` → error
- `src == nullptr` → error
- Validate `mode` is `AccelerationStructureCopyMode::Clone` or `AccelerationStructureCopyMode::Compact` → error if not

#### Step 8: `serializeAccelerationStructure`
Currently only does state checks. Add:
- `src == nullptr` → error
- `dst.buffer == nullptr` → error

#### Step 9: `deserializeAccelerationStructure`
Currently only does state checks. Add:
- `dst == nullptr` → error
- `src.buffer == nullptr` → error

#### Step 10: `setBufferState`
Currently only does state checks. Add:
- `buffer == nullptr` → error
- `state == ResourceState::Undefined` → `RHI_VALIDATION_WARNING("Setting buffer state to Undefined")`

#### Step 11: `setTextureState`
Currently only does state checks. Add:
- `texture == nullptr` → error
- Validate subresource range bounds against texture desc
- `state == ResourceState::Undefined` → warning

#### Step 12: `beginRenderPass`
Currently only does state checks. Add:
- `desc.colorAttachmentCount > 8` → `RHI_VALIDATION_ERROR("Too many color attachments (max 8)")`
- For each color attachment with `view != nullptr`:
  - Verify the view's texture has `RenderTarget` usage
  - Verify format is a color format (not depth/stencil)
- If `desc.depthStencilAttachment` has a non-null `view`:
  - Verify the view's texture has `DepthStencil` usage (check backend-specific requirements from existing `clearTextureDepthStencil` validation)
  - Verify format is a depth/stencil format
- Verify sample count is consistent across all attachments
- Validate `LoadOp` and `StoreOp` enum values are in range

### Verification

- Build: `cmake --build ./build --config Debug`
- Run tests: `./build/Debug/slang-rhi-tests.exe` — all existing tests must pass
- Ensure new validation only fires on genuinely invalid input, not on anything the test suite exercises
- Run `pre-commit run --all-files` to fix formatting

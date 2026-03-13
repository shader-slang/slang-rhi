## Phase 3: DebugRenderPassEncoder Validation — [debug-command-encoder.cpp](src/debug-layer/debug-command-encoder.cpp)

Add pipeline-bound tracking and parameter validation to `DebugRenderPassEncoder` methods that currently only do `requireOpen()` + `requireRenderPass()` state checks.

### Context

- `DebugRenderPassEncoder` wraps `IRenderPassEncoder`. All methods already call state-check helpers.
- A new `bool m_pipelineBound = false` field will need to be added to track whether `bindPipeline` has been called. Reset it to `false` when the pass begins.
- A new `bool m_indexBufferBound = false` field to track whether an index buffer has been set via `setRenderState`.
- Use `RHI_VALIDATION_ERROR(...)` / `RHI_VALIDATION_WARNING(...)` consistent with the rest of the debug layer.
- **Documentation**: After implementing validation for each step, update the corresponding method's doc comment in [slang-rhi.h](include/slang-rhi.h) to document parameter constraints, valid usage rules, and error conditions. Use the validation checks as the source of truth for what to document.
- **Research**: The bullet points in each step are an initial starting point only. Before implementing each step, read the method's implementation across all backends (`src/vulkan/`, `src/d3d12/`, `src/metal/`, `src/wgpu/`, `src/cpu/`, `src/cuda/`) and the shared base in `src/` to fully understand valid parameter ranges, implicit assumptions, and failure modes. This deeper understanding should inform both better validation checks and more accurate documentation.

### Steps

#### Step 1: `bindPipeline`
Currently only does state checks and wraps the root shader object. Add:
- `pipeline == nullptr` → `RHI_VALIDATION_ERROR("bindPipeline: pipeline is null")`
- Verify `pipeline` is a render pipeline (not compute or ray tracing) → error if wrong type
- Set `m_pipelineBound = true` after successful bind

#### Step 2: `setRenderState`
Currently only does state checks. Add:
- `state.viewportCount > 16` → `RHI_VALIDATION_ERROR("Too many viewports (max 16)")`
- `state.scissorRectCount > 16` → `RHI_VALIDATION_ERROR("Too many scissor rects (max 16)")`
- `state.vertexBufferCount > 16` → `RHI_VALIDATION_ERROR("Too many vertex buffers (max 16)")`
- For each viewport: `width <= 0 || height <= 0` → warning
- For each vertex buffer binding: `buffer == nullptr` → warning
- If index buffer is set: format must be `Format::R16_UINT` or `Format::R32_UINT` → error if not
- Track `m_indexBufferBound = (state.indexBuffer.buffer != nullptr)`

#### Step 3: `draw`
Currently only does state checks. Add:
- `!m_pipelineBound` → `RHI_VALIDATION_ERROR("draw: no pipeline bound")`
- `args.instanceCount == 0` → `RHI_VALIDATION_WARNING("draw: instanceCount is 0")`

#### Step 4: `drawIndexed`
Currently only does state checks. Add:
- `!m_pipelineBound` → `RHI_VALIDATION_ERROR("drawIndexed: no pipeline bound")`
- `!m_indexBufferBound` → `RHI_VALIDATION_ERROR("drawIndexed: no index buffer bound")`
- `args.instanceCount == 0` → `RHI_VALIDATION_WARNING("drawIndexed: instanceCount is 0")`

#### Step 5: `drawIndirect`
Currently only does state checks. Add:
- `!m_pipelineBound` → `RHI_VALIDATION_ERROR("drawIndirect: no pipeline bound")`
- `args.argBuffer == nullptr` → `RHI_VALIDATION_ERROR("drawIndirect: argBuffer is null")`
- `args.maxDrawCount == 0` → `RHI_VALIDATION_WARNING("drawIndirect: maxDrawCount is 0")`

#### Step 6: `drawIndexedIndirect`
Currently only does state checks. Add:
- `!m_pipelineBound` → error
- `!m_indexBufferBound` → error
- `args.argBuffer == nullptr` → error
- `args.maxDrawCount == 0` → warning

#### Step 7: `drawMeshTasks`
Currently only does state checks. Add:
- `!m_pipelineBound` → error
- `args.x == 0 || args.y == 0 || args.z == 0` → warning (no-op dispatch)

### Verification

- Build: `cmake --build ./build --config Debug`
- Run tests: `./build/Debug/slang-rhi-tests.exe` — all existing tests must pass
- Ensure new validation only fires on genuinely invalid input, not on anything the test suite exercises
- Run `pre-commit run --all-files` to fix formatting

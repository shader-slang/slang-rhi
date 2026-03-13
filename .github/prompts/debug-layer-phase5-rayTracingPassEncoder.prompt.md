## Phase 5: DebugRayTracingPassEncoder Validation — [debug-command-encoder.cpp](src/debug-layer/debug-command-encoder.cpp)

Add pipeline-bound tracking and parameter validation to `DebugRayTracingPassEncoder` methods.

### Context

- `DebugRayTracingPassEncoder` wraps `IRayTracingPassEncoder`. All methods already call state-check helpers.
- A new `bool m_pipelineBound = false` field will need to be added (same pattern as Phases 3–4).
- Track whether a shader table was provided during `bindPipeline`.
- Use `RHI_VALIDATION_ERROR(...)` / `RHI_VALIDATION_WARNING(...)` consistent with the rest of the debug layer.
- **Documentation**: After implementing validation for each step, update the corresponding method's doc comment in [slang-rhi.h](include/slang-rhi.h) to document parameter constraints, valid usage rules, and error conditions. Use the validation checks as the source of truth for what to document.
- **Research**: The bullet points in each step are an initial starting point only. Before implementing each step, read the method's implementation across all backends (`src/vulkan/`, `src/d3d12/`, `src/metal/`, `src/wgpu/`, `src/cpu/`, `src/cuda/`) and the shared base in `src/` to fully understand valid parameter ranges, implicit assumptions, and failure modes. This deeper understanding should inform both better validation checks and more accurate documentation.

### Steps

#### Step 1: `bindPipeline`
Currently only does state checks and wraps the root shader object. Add:
- `pipeline == nullptr` → `RHI_VALIDATION_ERROR("bindPipeline: pipeline is null")`
- `shaderTable == nullptr` → `RHI_VALIDATION_ERROR("bindPipeline: shaderTable is null")`
- Verify `pipeline` is a ray tracing pipeline (not render or compute) → error if wrong type
- Set `m_pipelineBound = true` after successful bind
- Store a reference to the shader table for subsequent validation

#### Step 2: `dispatchRays`
Currently only does state checks. Add:
- `!m_pipelineBound` → `RHI_VALIDATION_ERROR("dispatchRays: no pipeline bound")`
- `rayGenShaderIndex >= shaderTable.rayGenShaderCount` → `RHI_VALIDATION_ERROR("dispatchRays: rayGenShaderIndex out of range")`
- `width == 0 || height == 0 || depth == 0` → `RHI_VALIDATION_WARNING("dispatchRays: one or more dispatch dimensions is 0")`

### Verification

- Build: `cmake --build ./build --config Debug`
- Run tests: `./build/Debug/slang-rhi-tests.exe` — all existing tests must pass
- Ensure new validation only fires on genuinely invalid input, not on anything the test suite exercises
- Run `pre-commit run --all-files` to fix formatting

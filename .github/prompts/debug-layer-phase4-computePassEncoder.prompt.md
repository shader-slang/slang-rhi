## Phase 4: DebugComputePassEncoder Validation — [debug-command-encoder.cpp](src/debug-layer/debug-command-encoder.cpp)

Add pipeline-bound tracking and parameter validation to `DebugComputePassEncoder` methods.

### Context

- `DebugComputePassEncoder` wraps `IComputePassEncoder`. All methods already call state-check helpers.
- A new `bool m_pipelineBound = false` field will need to be added (same pattern as Phase 3).
- Use `RHI_VALIDATION_ERROR(...)` / `RHI_VALIDATION_WARNING(...)` consistent with the rest of the debug layer.

### Steps

#### Step 1: `bindPipeline`
Currently only does state checks and wraps the root shader object. Add:
- `pipeline == nullptr` → `RHI_VALIDATION_ERROR("bindPipeline: pipeline is null")`
- Verify `pipeline` is a compute pipeline (not render or ray tracing) → error if wrong type
- Set `m_pipelineBound = true` after successful bind

#### Step 2: `dispatchCompute`
Currently only does state checks. Add:
- `!m_pipelineBound` → `RHI_VALIDATION_ERROR("dispatchCompute: no pipeline bound")`
- `x == 0 || y == 0 || z == 0` → `RHI_VALIDATION_WARNING("dispatchCompute: one or more group dimensions is 0")`

#### Step 3: `dispatchComputeIndirect`
Currently only does state checks. Add:
- `!m_pipelineBound` → `RHI_VALIDATION_ERROR("dispatchComputeIndirect: no pipeline bound")`
- `argBuffer == nullptr` → `RHI_VALIDATION_ERROR("dispatchComputeIndirect: argBuffer is null")`

### Verification

- Build: `cmake --build ./build --config Debug`
- Run tests: `./build/Debug/slang-rhi-tests.exe` — all existing tests must pass
- Ensure new validation only fires on genuinely invalid input, not on anything the test suite exercises
- Run `pre-commit run --all-files` to fix formatting

# Plan: Parallel Code Generation & Pipeline Creation

## TL;DR

Parallelize shader compilation (`getEntryPointCode`) and backend pipeline creation (`createXxxPipeline2`) using the existing `ITaskPool` infrastructure. The key change is rewriting `resolvePipelines()` to collect all pipeline work, submit it as parallel tasks with dependencies, and wait for completion — replacing the current sequential `getConcretePipeline()` loop. Thread safety must be added to `ShaderCache`, `ShaderProgram::m_specializedPrograms`, and entry point compilation paths.

## Context & Current Architecture

**Current flow (sequential):**
1. `CommandEncoder::finish()` calls `resolvePipelines(device)`
2. `resolvePipelines()` iterates commands sequentially
3. For each pipeline command, calls `getConcretePipeline()` which:
   - Specializes program (fast, Slang API)
   - Compiles shaders via `ShaderProgram::compileShaders()` → `getEntryPointCodeFromShaderCache()` → `getEntryPointCode()` (SLOW)
   - Creates backend pipeline via `createXxxPipeline2()` (medium cost, involves driver compilation)
   - Caches result

**Parallelization opportunities:**
- `getEntryPointCode()` — Slang's code generation is the biggest cost; Slang will support parallel calls
- `createXxxPipeline2()` — native API pipeline creation (D3D12 PSO, Vulkan pipeline, Metal pipeline) can run in parallel
- Multiple pipelines in a single command list can be resolved simultaneously
- Multiple entry points within a single program can be compiled simultaneously

**Existing infrastructure:**
- `ITaskPool` interface with DAG-based dependency scheduling
- `ThreadedTaskPool` with worker threads and dependency tracking
- `globalTaskPool()` accessor with lazy initialization
- `IDevice::pushCudaContext()`/`popCudaContext()` for CUDA thread safety

**Thread safety gaps (must fix):**
- `ShaderCache::specializedPipelines` — unprotected `unordered_map`
- `ShaderCache::componentIds` — unprotected map
- `ShaderProgram::m_specializedPrograms` — marked `TODO make thread-safe`
- `Device::getEntryPointCodeFromShaderCache()` — no locking around persistent cache access
- `Device::m_shaderCompilationReporter` — already thread-safe (has mutex)

## Steps

### Phase 1: Thread Safety Foundation

**Step 1. Add mutex to `ShaderCache`**
- File: `src/device.h` (ShaderCache class)
- Add `std::mutex m_mutex`
- Protect `getComponentId()`, `getSpecializedPipeline()`, `addSpecializedPipeline()` with `std::lock_guard`

**Step 2. Add mutex to `ShaderProgram::m_specializedPrograms`**
- File: `src/shader.h`
- Add `std::mutex m_specializationMutex`
- Protect access in `Device::getSpecializedProgram()` (device.cpp ~L159)

**Step 3. Make `getEntryPointCodeFromShaderCache()` thread-safe**
- File: `src/device.cpp`
- The persistent shader cache (`IPersistentCache`) is user-provided; document that it must be thread-safe
- Add mutex around the ShaderProgram's entry point data (or ensure `createShaderModule` is not called from this function — currently it isn't)

### Phase 2: Parallel Shader Compilation Infrastructure

**Step 4. Add a `compileEntryPoint()` free function or method**
- File: `src/shader.cpp`
- Extract the lambda inside `compileShaders()` into a standalone function that:
  - Takes (Device*, ShaderProgram*, slang::IComponentType*, entryPointInfo, entryPointIndex)
  - Calls `getEntryPointCodeFromShaderCache()`
  - Returns Result + ComPtr<ISlangBlob> (kernel code) + entryPointInfo
  - Does NOT call `createShaderModule()` (deferred to caller)

**Step 5. Add `compileEntryPointsParallel()` to `ShaderProgram`**
- File: `src/shader.cpp`, `src/shader.h`
- New method that:
  - Iterates entry points (same logic as current `compileShaders()`)
  - Submits one task per entry point to the global task pool
  - Each task calls the standalone `compileEntryPoint()` function
  - Returns a vector of task handles + work item structs
- Keep `compileShaders()` as a convenience wrapper that calls `compileEntryPointsParallel()` then waits + calls `createShaderModule()` sequentially (preserving backward compatibility for non-batch callers)

**Step 6. Define `PipelineWorkItem` struct**
- File: `src/command-buffer.h` or new `src/pipeline-resolver.h`
- Struct containing:
  - Pointer to command entry (for patching)
  - Virtual pipeline + specialization args
  - Specialized program (output of step)
  - Compiled entry point blobs (output of compilation)
  - Concrete pipeline (output of creation)
  - Result code
  - Task handles for dependency tracking

### Phase 3: Parallel Pipeline Resolution

**Step 7. Rewrite `resolvePipelines()` for parallel execution**
- File: `src/command-buffer.cpp`
- New flow:

```
resolvePipelines(Device* device):
  ITaskPool* taskPool = globalTaskPool();

  // === Phase A: Collect & deduplicate ===
  // Scan commands for SetRenderState/SetComputeState/SetRayTracingState
  // Build PipelineWorkItem list
  // Skip already-concrete pipelines
  // Check caches — skip already-resolved
  // Deduplicate: same (virtualPipeline + specializationArgs) → single work item

  // If no work needed or only 1 item → fall back to sequential getConcretePipeline()

  // === Phase B: Specialize programs (sequential, fast) ===
  // For each work item needing specialization:
  //   specializeProgram() — fast Slang API call, not worth parallelizing

  // === Phase C: Parallel entry point compilation ===
  // Group work items by unique program
  // For each unique program, for each entry point:
  //   Submit compilation task to taskPool
  // Track task handles per program

  // === Phase D: Parallel pipeline creation (depends on C) ===
  // For each work item:
  //   Submit pipeline creation task with dependency on all compilation tasks of its program
  //   Task calls createShaderModule() then createXxxPipeline2()
  //   (createShaderModule is per-pipeline, not shared, so thread-safe)

  // === Phase E: Wait & collect results ===
  // Wait on all pipeline creation tasks
  // Check each work item's Result; return first failure

  // === Phase F: Finalize (sequential) ===
  // For each work item:
  //   Update command entry with concrete pipeline
  //   Update ShaderCache / virtual pipeline cache
```

**Step 8. Handle CUDA context in parallel pipeline tasks**
- File: `src/cuda/cuda-pipeline.cpp` or wrapper in `src/device.cpp`
- When device is CUDA, wrap pipeline creation task body with `pushCudaContext()` / `popCudaContext()`
- Alternative: add a virtual `Device::onWorkerThreadBegin()` / `Device::onWorkerThreadEnd()` pair that CUDA overrides
- This is cleaner — each backend decides what per-thread setup is needed

### Phase 4: Backend-Specific Considerations

**Step 9. D3D12 pipeline creation thread safety**
- D3D12's `ID3D12Device::CreateGraphicsPipelineState()` et al. are thread-safe by design
- No additional work needed for D3D12 pipeline creation itself
- Ensure `ShaderProgramImpl::createShaderModule()` (stores ShaderBinary) doesn't race — it's called per work item so should be fine

**Step 10. Vulkan pipeline creation thread safety**
- `vkCreateGraphicsPipelines()` / `vkCreateComputePipelines()` are thread-safe when called with different pipeline caches or VK_NULL_HANDLE cache
- `vkCreateShaderModule()` is thread-safe
- Ensure Vulkan's `ShaderProgramImpl::createShaderModule()` (creates VkShaderModule, scans SPIR-V) doesn't race

**Step 11. Metal pipeline creation thread safety**
- Metal's `MTLDevice.newRenderPipelineState()` etc. are thread-safe
- `MTLDevice.newLibrary()` is thread-safe
- No additional work needed

**Step 12. CUDA/OptiX pipeline creation thread safety**
- Compute: `cuModuleLoadData()` requires current CUDA context — handled by Step 8
- RT/OptiX: `optixModuleCreate()`, `optixProgramGroupCreate()`, `optixPipelineCreate()` — all require CUDA context
- OptiX operations may have internal serialization — may not benefit as much from parallelism
- Consider: for OptiX, it may be most beneficial to parallelize the shader compilation (PTX generation) while keeping pipeline creation sequential
- The `ContextImpl::createPipeline()` in optix-api-impl.cpp creates multiple OptixModules — these could potentially be parallelized internally too (future improvement)

### Phase 5: Error Handling & Fallback

**Step 13. Error propagation from parallel tasks**
- `ITaskPool` uses `void* payload` — store `Result` in the work item struct
- After waiting, iterate work items and return first failure
- On failure, release all task handles before returning

**Step 14. Fallback for single-pipeline case**
- If only 1 pipeline needs resolution, use the existing sequential path (no task pool overhead)
- If task pool is `BlockingTaskPool` (0 workers), all tasks execute synchronously anyway — no special handling needed

**Step 15. Fallback for non-batch callers**
- Keep `getConcretePipeline()` working as-is for any code that calls it directly
- It just becomes the single-item fast path

## Relevant Files

- `src/command-buffer.cpp` — rewrite `resolvePipelines()` with parallel orchestration
- `src/command-buffer.h` — add `PipelineWorkItem` struct and helpers
- `src/device.cpp` — add thread safety to `ShaderCache`, `getSpecializedProgram()`, `getEntryPointCodeFromShaderCache()`; add `onWorkerThreadBegin/End`
- `src/device.h` — add mutex members to `ShaderCache`; add virtual `onWorkerThreadBegin/End`
- `src/shader.cpp` — extract `compileEntryPoint()`, keep `compileShaders()` as wrapper
- `src/shader.h` — add `m_specializationMutex`, declare parallel compile helpers
- `src/pipeline.h` — referenced for Pipeline/VirtualPipeline types
- `src/cuda/cuda-device.cpp` — override `onWorkerThreadBegin/End` with CUDA context push/pop
- `src/cuda/cuda-pipeline.cpp` — CUDA pipeline creation (verify thread safety with context)
- `src/cuda/optix-api-impl.cpp` — OptiX pipeline creation (verify thread safety requirements)
- `src/core/task-pool.h` — existing task pool (no changes expected)
- `src/core/task-pool.cpp` — existing implementation (no changes expected)

## Verification

1. **Build**: `cmake --build ./build --config Debug` — verify clean compilation
2. **Unit test for parallel resolution**: Create test with multiple compute pipelines in single command encoder, verify all resolve correctly
3. **Specialization test**: Test with specializable programs compiled in parallel
4. **Run existing test suite**: `./build/Debug/slang-rhi-tests.exe` — all passing tests must still pass
5. **Thread sanitizer**: Build with TSan to detect data races in parallel path
6. **Performance benchmark**: Use `test-benchmark-command` or create new benchmark that measures time for resolving N pipelines sequentially vs. parallel
7. **CUDA-specific test**: Verify CUDA compute pipeline creation works when dispatched to worker threads
8. **Error handling test**: Verify that a compilation failure in one pipeline correctly propagates and doesn't crash
9. **Single pipeline fast path**: Verify no task pool overhead for single-pipeline case

## Decisions

- **Specialization is sequential**: `specializeProgram()` calls `slang::specialize()` which is fast and touches shared Slang state; not worth parallelizing
- **Entry point compilation is the primary target**: `getEntryPointCode()` is the most expensive operation and the one Slang is adding parallel support for
- **Pipeline creation is secondary target**: Worth parallelizing for D3D12/Vulkan where driver compiles PSO/pipeline
- **CUDA context handled via virtual method**: `onWorkerThreadBegin/End` pattern rather than CUDA-specific checks in common code
- **Deduplication scope**: Per `resolvePipelines()` call (i.e., per command encoder). Cross-encoder dedup handled by existing caches
- **Backward compatible**: `getConcretePipeline()` and `compileShaders()` remain functional for non-batch callers
- **Excluded**: Parallelizing within OptiX's multi-module pipeline creation (future work)
- **Excluded**: Async pipeline compilation across frames (fire-and-forget with fallback pipeline)

## Further Considerations

1. **Should `createShaderModule()` be called inside the parallel task or after waiting?** Recommendation: Inside the pipeline creation task, since each pipeline has its own `ShaderProgram` copy (after specialization) or the modules are per-backend and already stored in a vector that won't be shared. This avoids an extra sequential phase.

2. **Should we add a `ParallelPipelineResolver` class or keep it as methods on `CommandEncoder`?** Recommendation: A helper class (`PipelineResolver`) keeps `CommandEncoder` clean and allows reuse. It would own the work items and orchestrate the task pool.

3. **CUDA context push/pop overhead**: Pushing/popping CUDA context on each worker thread for each task has some overhead. Alternative: use thread-local CUDA context caching (push once per thread, pop on thread exit). The `onWorkerThreadBegin/End` pattern naturally supports this if the task pool reuses threads (which `ThreadedTaskPool` does).

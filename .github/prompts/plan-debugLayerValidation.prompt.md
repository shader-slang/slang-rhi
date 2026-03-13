## Debug Layer Validation — Master Plan

**TL;DR:** The debug layer currently has ~153 methods, of which only ~40 do meaningful validation. This plan adds validation to ~60 methods across 6 phases, one per interface. Each phase is a separate prompt file with per-method steps.

### Phases

| Phase | Interface | File | Steps | Priority |
|-------|-----------|------|-------|----------|
| 1 | `DebugDevice` | [debug-layer-phase1-device.prompt.md](.github/prompts/debug-layer-phase1-device.prompt.md) | 12 | High |
| 2 | `DebugCommandEncoder` | [debug-layer-phase2-commandEncoder.prompt.md](.github/prompts/debug-layer-phase2-commandEncoder.prompt.md) | 12 | High |
| 3 | `DebugRenderPassEncoder` | [debug-layer-phase3-renderPassEncoder.prompt.md](.github/prompts/debug-layer-phase3-renderPassEncoder.prompt.md) | 7 | High |
| 4 | `DebugComputePassEncoder` | [debug-layer-phase4-computePassEncoder.prompt.md](.github/prompts/debug-layer-phase4-computePassEncoder.prompt.md) | 3 | High |
| 5 | `DebugRayTracingPassEncoder` | [debug-layer-phase5-rayTracingPassEncoder.prompt.md](.github/prompts/debug-layer-phase5-rayTracingPassEncoder.prompt.md) | 2 | High |
| 6 | `DebugHeap` | [debug-layer-phase6-heap.prompt.md](.github/prompts/debug-layer-phase6-heap.prompt.md) | 2 | Low |

### Scope

- Limited to existing debug-layer methods (not adding `DebugBuffer`, `DebugTexture`, etc.)
- All backends referenced (Vulkan, D3D12, Metal, WebGPU, CPU, CUDA) for a complete picture
- Phases 3–5 require new pipeline-bound tracking state in pass encoder classes
- Phase 1 Step 9 requires per-buffer map-state tracking in `DebugDevice`

### Interfaces already well-validated (no changes needed)

| Class | Status |
|-------|--------|
| `DebugCommandQueue` | `submit` already validates null command buffers |
| `DebugShaderObject` | All setters already check finalized state |
| `DebugFence` | `setCurrentValue` already validates signal value |
| `DebugQueryPool` | `getResult` already validates index bounds |
| `DebugSurface` | Full configure/acquire/present state machine |

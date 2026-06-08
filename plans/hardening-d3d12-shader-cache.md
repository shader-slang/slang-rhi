# D3D12 Shader Cache Device Creation Hardening Plan

## Goal
Reproduce and fix D3D12 shader-cache failures around device creation and persistent cache state.

## Scope
- `shader-cache-source-string.d3d12`.
- `shader-cache-specialization.d3d12`.
- Repeated create/destroy behavior with persistent shader cache.
- Shared global state and cache directory isolation.

## Work Items
1. Capture full D3D12 device creation diagnostics: HRESULTs, adapter, attempted feature levels, shader model, and debug callback output.
2. Verify shader-cache tests use isolated cache directories and do not depend on shared global state.
3. Stress repeated device create/destroy with persistent cache enabled.
4. Preserve generated shader-cache sources, cache stats, and temp directories on failure.
5. Land targeted fixes for confirmed cache isolation or device initialization issues.

## Validation
- `slang-rhi-tests -tc="shader-cache-source-string.d3d12,shader-cache-specialization.d3d12" -check-devices` passes repeated local/CI stress runs.
- Logs show enough device creation detail to distinguish feature support issues from cache/test harness issues.
- Repeated create/destroy with persistent shader cache does not leak state between test cases.

## Open Questions
- Are failures tied to a particular adapter selection path or feature-level fallback?
- Should cache directories include test name, process ID, and device type to avoid cross-run collision?

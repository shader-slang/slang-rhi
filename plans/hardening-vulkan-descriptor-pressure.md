# Vulkan Descriptor Pressure Hardening Plan

## Goal
Reproduce and fix failures around `shader-object-large.vulkan`, with enough instrumentation to understand descriptor pool sizing, set allocation, and binding cache behavior.

## Scope
- Descriptor pool sizing and exhaustion paths.
- Descriptor set allocation failures.
- Resource binding count assumptions.
- Per-command binding cache lifetime.

## Work Items
1. Review `shader-object-large` resource counts against Vulkan descriptor limits and backend pool sizing logic.
2. Add diagnostics for descriptor pool creation, descriptor set allocation failures, and binding count pressure.
3. Verify descriptor/binding caches are scoped correctly across command recording, submission, and reset.
4. Stress the test with repeat loops and capture adapter/device limits for each failure.
5. Land targeted fixes only after a specific root cause is confirmed.

## Validation
- `slang-rhi-tests -tc="shader-object-large.vulkan" -check-devices` passes repeated local/CI stress runs.
- Failure logs include descriptor limits, pool sizes, allocation counts, `VkResult`, and adapter/device info.
- Any fix includes a focused regression test or a clear stress-loop validation note.

## Open Questions
- Is the current failure deterministic on a specific Vulkan driver or only under CI scheduling pressure?
- Should the default descriptor pool policy grow, retry, or fail fast with better diagnostics?

# Slang RHI CI Hardening Plan

## Summary
Two-week stabilization push, with current tests staying blocking and a whole-repo AI/code review included. The priority is to make failures diagnosable first, reproduce the three recent flakes under stress, then land targeted fixes for confirmed root causes.

Success criteria:
- Recent failures either stop reproducing or produce actionable logs with backend result codes, adapter/device info, and test context.
- `shader-object-large.vulkan`, `shader-cache-*.d3d12`, and `task-pool-threaded` pass repeated local/CI stress runs.
- No current flaky test is quarantined or made non-blocking.

## Key Changes
- Improve failure diagnostics:
  - Include `VkResult` name/value and Vulkan callsite in `SLANG_VK_RETURN_ON_FAIL` / `SLANG_VK_CHECK`.
  - Include D3D12 HRESULTs, selected adapter, attempted feature levels, shader model, and debug callback output on `createDevice` failures.
  - Upgrade `REQUIRE_CALL`/`CHECK_CALL` test macros or add test-only variants that print numeric `Result`, expression text, device type, and current test name.
  - Preserve failing test temp directories/artifacts on failure, especially shader-cache generated sources and cache stats.

- Target current failure surfaces:
  - Vulkan descriptor/binding pressure: review `shader-object-large` against descriptor pool sizing, descriptor set allocation, resource binding counts, and per-command binding cache lifetime.
  - D3D12 shader-cache device creation: isolate cache tests from shared global state, capture device creation diagnostics, and verify repeated create/destroy with persistent shader cache.
  - Task pool concurrency: review submit/wait/release/destructor ownership, task group lifetime, dependency retention, and worker shutdown. Add ThreadSanitizer-capable Linux job if CMake support is practical in this push.

- Whole-repo AI review:
  - Split review into four passes: backend resource lifetime, error handling/diagnostics, threading/global state, and test harness/CI reliability.
  - Triage findings into P0/P1/P2; fix P0/P1 that plausibly affect CI flakes in this push, file follow-ups for broader cleanup.

- CI hardening:
  - Keep existing CI tests blocking.
  - Add a manual/scheduled hardening workflow that runs targeted repeat loops for the three failing areas and uploads logs/artifacts.
  - Add clear per-job device inventory output and failure summaries.

## Test Plan
- Run builds/tests outside the sandbox per repo instructions.
- Targeted repeat runs:
  - `slang-rhi-tests -tc="shader-object-large.vulkan" -check-devices`
  - `slang-rhi-tests -tc="shader-cache-source-string.d3d12,shader-cache-specialization.d3d12" -check-devices`
  - `slang-rhi-tests -tc="task-pool-threaded"`, repeated enough to expose teardown races.
- Backend coverage:
  - Linux x86_64 clang Debug Vulkan.
  - Windows x86_64 MSVC Release D3D12.
  - Windows x86_64 clang Debug task-pool/ASAN config.
- Add focused regression tests only where fixes land; avoid adding heavy stress cases to default CI unless runtime remains modest.

## Assumptions
- No public Slang RHI API change is required.
- Test harness/CI interfaces may gain small options such as repeat count, keep-temp-on-failure, or artifact directory.
- Current flaky tests remain blocking; no quarantine or pass-on-rerun policy.
- Whole-repo AI review is used for triage and risk discovery, but implementation focus stays on the current CI failures first.

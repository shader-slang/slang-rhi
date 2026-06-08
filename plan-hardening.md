# Slang RHI CI Hardening Plan

## Summary
This is the umbrella plan for a two-week stabilization push. Current CI tests stay blocking. The work is split into focused hardening axes so each area can be reviewed, owned, and iterated independently.

Success criteria:
- Recent failures either stop reproducing or produce actionable logs with backend result codes, adapter/device info, and test context.
- `shader-object-large.vulkan`, `shader-cache-*.d3d12`, and `task-pool-threaded` pass repeated local/CI stress runs.
- No current flaky test is quarantined or made non-blocking.

## Hardening Axes
1. [Failure diagnostics](plans/hardening-diagnostics.md)
2. [Vulkan descriptor pressure](plans/hardening-vulkan-descriptor-pressure.md)
3. [D3D12 shader cache device creation](plans/hardening-d3d12-shader-cache.md)
4. [Task pool concurrency](plans/hardening-task-pool-concurrency.md)
5. [Whole-repo AI review](plans/hardening-ai-review.md)
6. [CI workflow hardening](plans/hardening-ci.md)

## Suggested Sequence
1. Land diagnostics first so later failures are easier to understand.
2. Run targeted stress loops for the three known failure surfaces.
3. Fix confirmed root causes in Vulkan, D3D12, and task pool areas.
4. Run the AI review passes in parallel with targeted debugging, then only pull P0/P1 CI-relevant fixes into this push.
5. Add scheduled/manual hardening CI once the local commands and artifact layout are stable.

## Shared Test Plan
Builds and tests must be run outside the sandbox per repository instructions.

Targeted repeat runs:
- `slang-rhi-tests -tc="shader-object-large.vulkan" -check-devices`
- `slang-rhi-tests -tc="shader-cache-source-string.d3d12,shader-cache-specialization.d3d12" -check-devices`
- `slang-rhi-tests -tc="task-pool-threaded"`

Backend coverage:
- Linux x86_64 clang Debug Vulkan.
- Windows x86_64 MSVC Release D3D12.
- Windows x86_64 clang Debug task-pool/ASAN config.

## Shared Assumptions
- No public Slang RHI API change is required.
- Test harness/CI interfaces may gain small options such as repeat count, keep-temp-on-failure, or artifact directory.
- Current flaky tests remain blocking; no quarantine or pass-on-rerun policy.
- Whole-repo AI review is used for triage and risk discovery, but implementation focus stays on current CI failures first.

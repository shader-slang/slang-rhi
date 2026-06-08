# Failure Diagnostics Hardening Plan

## Goal
Make CI failures self-explanatory enough to distinguish device/setup issues, backend API failures, test harness misuse, and true race or lifetime bugs.

## Scope
- Vulkan error reporting.
- D3D12 device creation diagnostics.
- Test assertion output.
- Failed-test artifact preservation.
- A short implementation-first pass, with broader failure summaries treated as stretch work.

## Expected Touchpoints
- `src/vulkan/vk-utils.h`: update `SLANG_VK_RETURN_ON_FAIL` and `SLANG_VK_CHECK` to pass expression, file, and line information.
- `src/vulkan/vk-utils.cpp`: add `VkResult` name/value formatting and richer Vulkan failure reporting.
- `src/d3d12/d3d12-device.cpp`: capture D3D12 device creation attempts, HRESULTs, adapter details, feature-level attempts, shader model probing, and debug output.
- `tests/testing.h`: replace or wrap `REQUIRE_CALL` and `CHECK_CALL` so failed calls report the evaluated expression and returned `Result`.
- `tests/testing.cpp`: add helpers for current test/device context, captured debug output, and test temp-directory retention.
- `tests/main.cpp`: parse any new test options and only clean up temp directories when policy allows it.
- `tests/test-shader-cache.cpp`: stop deleting shader-cache temp directories on failure when retention is enabled.

## Work Items
1. Include `VkResult` name/value and Vulkan callsite in `SLANG_VK_RETURN_ON_FAIL` and `SLANG_VK_CHECK`.
2. Include D3D12 HRESULTs, selected adapter, attempted feature levels, shader model, and debug callback output on `createDevice` failures.
3. Upgrade `REQUIRE_CALL` and `CHECK_CALL` in the test harness first, without changing common production `SLANG_RETURN_ON_FAIL` behavior.
4. Preserve failing test temp directories/artifacts on failure, especially shader-cache generated sources and cache stats.
5. Add a short failure summary block at test shutdown only if the harness can do this without broad churn.

## Output Contract
When a backend or test assertion fails, the log should include as much of the following context as is available without expensive work on the success path:

- Expression text, source file, and source line.
- Numeric `Result` value for RHI calls.
- Symbolic result name when available, such as `VK_ERROR_OUT_OF_POOL_MEMORY` for Vulkan.
- Raw backend value, such as `VkResult` or D3D12 `HRESULT`.
- Current test suite and test case.
- Current device type.
- Selected adapter index, name, type, and LUID when available.
- D3D12 feature levels attempted and the HRESULT for each attempt.
- D3D12 highest shader model requested and highest shader model detected.
- Captured debug callback output from device creation or shader compilation.
- Path to retained test artifacts when a failure causes temp preservation.

## Artifact Preservation Policy
- Add both a command-line option and an environment variable:
  - CLI: `--keep-temp-on-failure`
  - Env var: `SLANG_RHI_KEEP_TEST_TEMP_ON_FAILURE=1`
- Keep successful-run behavior unchanged: temp directories are removed by default.
- On failure with preservation enabled, retain the whole current test temp root and print its path.
- For shader-cache tests, retain the per-case temp directory when the test fails, including generated `.slang` sources and cache stats.
- Do not retain artifacts by default for passing tests, even when the preservation option is enabled.

## Implementation Order
1. Add small formatting helpers for Vulkan result names and values.
2. Update Vulkan macros and callsites through macro arguments rather than manually editing every callsite.
3. Add test-harness call assertion helpers and update `REQUIRE_CALL`/`CHECK_CALL`.
4. Add D3D12 device creation diagnostics around `D3D12CreateDevice` attempts.
5. Add temp-directory preservation policy and shader-cache failure retention.
6. Revisit the optional end-of-run failure summary after the core diagnostics are working.

## Validation
- Add or use a small local-only/fault-injection path to verify Vulkan output includes `VkResult` name/value, expression, callsite, test name, and device type.
- Force or simulate one D3D12 device creation failure and confirm adapter, feature-level attempts, shader model details, HRESULTs, and debug callback details are visible.
- Trigger a failing `REQUIRE_CALL`/`CHECK_CALL` path and confirm expression text, numeric `Result`, current test, and device type are printed.
- Run one shader-cache failure with `--keep-temp-on-failure` or `SLANG_RHI_KEEP_TEST_TEMP_ON_FAILURE=1` and confirm generated sources/cache artifacts are retained and their path is printed.
- Confirm successful tests do not retain temp directories by default.

## Open Questions
- Should the final failure summary be implemented in this pass, or deferred until the targeted hardening workflow exists?
- Should D3D12 diagnostics include debug-layer info queue messages at creation time if that can be done without enabling extra validation globally?

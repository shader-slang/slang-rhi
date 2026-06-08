# Failure Diagnostics Hardening Plan

## Goal
Make CI failures self-explanatory enough to distinguish device/setup issues, backend API failures, test harness misuse, and true race or lifetime bugs.

## Scope
- Vulkan error reporting.
- D3D12 device creation diagnostics.
- Test assertion output.
- Failed-test artifact preservation.

## Work Items
1. Include `VkResult` name/value and Vulkan callsite in `SLANG_VK_RETURN_ON_FAIL` and `SLANG_VK_CHECK`.
2. Include D3D12 HRESULTs, selected adapter, attempted feature levels, shader model, and debug callback output on `createDevice` failures.
3. Upgrade `REQUIRE_CALL` and `CHECK_CALL`, or add test-only variants, to print numeric `Result`, expression text, device type, and current test name.
4. Preserve failing test temp directories/artifacts on failure, especially shader-cache generated sources and cache stats.
5. Add a short failure summary block at test shutdown if the harness can do this without broad churn.

## Validation
- Force or simulate one Vulkan failure and confirm the output includes `VkResult`, callsite, test name, and device type.
- Force or simulate one D3D12 device creation failure and confirm adapter/features/debug callback details are visible.
- Confirm successful tests do not retain temp directories by default.

## Open Questions
- Should artifact preservation be controlled by an env var, a command-line option, or both?
- Should assertion macro changes be test-only first, then promoted to common helpers after they prove useful?

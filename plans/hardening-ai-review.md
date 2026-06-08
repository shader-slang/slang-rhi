# Whole-Repo AI Review Hardening Plan

## Goal
Use a whole-repo review to discover risk areas that plausibly explain CI flakes, while keeping implementation focused on the current failures.

## Review Passes
1. Backend resource lifetime.
2. Error handling and diagnostics.
3. Threading and global state.
4. Test harness and CI reliability.

## Triage
- P0: Likely CI flake cause, data race, use-after-free, or missing synchronization.
- P1: Plausible CI flake contributor or severe diagnostic blind spot.
- P2: Broader cleanup, maintainability, or low-probability issue.

## Work Items
1. Run each review pass separately and keep findings grouped by pass.
2. Require file/line references and a concrete failure mode for each P0/P1 finding.
3. Fix P0/P1 findings that plausibly affect current CI flakes during this push.
4. File follow-ups for P2 or broad cleanup outside this stabilization window.
5. Cross-link accepted findings to the relevant axis plan.

## Validation
- Every implemented finding has either a regression test, stress-loop evidence, or a clear reason tests are not practical.
- Review output is reduced to actionable issues before implementation starts.

## Open Questions
- Where should review findings live: issue tracker, a markdown log, or per-axis plan updates?
- Should AI review include generated CI logs once the hardening workflow exists?

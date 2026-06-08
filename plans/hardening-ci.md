# CI Workflow Hardening Plan

## Goal
Add CI coverage that makes the known flakes easier to reproduce and diagnose without weakening the current blocking test suite.

## Scope
- Manual or scheduled hardening workflow.
- Targeted repeat loops for known failure surfaces.
- Device inventory and failure summaries.
- Artifact upload for failed runs.

## Work Items
1. Keep existing CI tests blocking.
2. Add a manual/scheduled hardening workflow for targeted repeat loops.
3. Emit per-job device inventory before tests run.
4. Upload logs and preserved artifacts for failures.
5. Keep heavy stress cases out of default CI unless runtime remains modest.

## Validation
- Hardening workflow can be run manually and on schedule.
- Each job prints device inventory, command lines, repeat counts, and failure summaries.
- Failed runs upload logs, temp directories, shader-cache artifacts, and any backend diagnostic output.

## Open Questions
- What repeat count gives useful signal without making scheduled CI too expensive?
- Should hardening workflow failures notify maintainers differently from default CI failures?

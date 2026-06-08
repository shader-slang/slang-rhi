# Task Pool Concurrency Hardening Plan

## Goal
Reproduce and fix `task-pool-threaded` failures, especially teardown races and lifetime bugs around task groups, dependencies, and worker shutdown.

## Scope
- Task submission and wait behavior.
- Task group ownership and lifetime.
- Dependency retention.
- Worker shutdown and destructor sequencing.
- Optional sanitizer coverage.

## Work Items
1. Review submit/wait/release/destructor ownership for races and missed synchronization.
2. Review task group lifetime and dependency retention across worker execution.
3. Add focused logging or assertions around shutdown sequencing if needed.
4. Stress `task-pool-threaded` with repeat loops, including high iteration counts for teardown paths.
5. Add a ThreadSanitizer-capable Linux job if CMake support is practical in this push.

## Validation
- `slang-rhi-tests -tc="task-pool-threaded"` passes repeated local/CI stress runs.
- Any lifetime fix has a focused regression test or a stress-loop validation note.
- If added, the sanitizer job reports cleanly or produces actionable findings without blocking default CI until it is stable.

## Open Questions
- Is the failure more likely during normal task completion or pool destruction?
- Should sanitizer coverage be a scheduled hardening job first rather than a default blocking job?

---
name: slang-rhi-compile-research
description: Run measured compile-time include-reduction experiments for the slang-rhi repository. Use when Codex is asked to launch, continue, or evaluate the slang-rhi compile autoresearch loop; reduce C++ compile time; investigate heavy includes, STL header fanout, or slow translation units; or apply small include-cleanup candidates without using precompiled headers or unity builds.
---

# Slang RHI Compile Research

## Core Rule

Use the repo-local harness at `tools/compile-research/compile_research.py` as the source of truth. Do not copy the harness into the skill. Do not use precompiled headers or unity builds as solutions.

Default to the existing Windows MSVC Debug build:

```powershell
python tools/compile-research/compile_research.py --build-dir build --config Debug rank --top 25 --json build/compile-research/rank.json
```

## Workflow

1. Inspect `git status --short` and protect unrelated user edits.
2. Run `rank` to identify slow TUs and high-cost local headers.
3. Choose one narrow candidate, usually a header family such as `src/core/common.h`, a backend `*-base.h`, or a public header fanout issue.
4. Capture a baseline probe for the affected TUs before editing:

```powershell
python tools/compile-research/compile_research.py --build-dir build --config Debug probe --header src/core/common.h --limit 10 --iterations 3 --json build/compile-research/common-baseline.json
```

5. Apply one conservative include refactor.
6. Run the same probe again and compare:

```powershell
python tools/compile-research/compile_research.py compare build/compile-research/common-baseline.json build/compile-research/common-candidate.json --markdown build/compile-research/common-comparison.md
```

7. Keep the patch only when compile time improves materially, or transitive include count drops without timing regression.
8. Record the experiment:

```powershell
python tools/compile-research/compile_research.py record-experiment --name split-common-math --path src/core --baseline build/compile-research/common-baseline.json --candidate build/compile-research/common-candidate.json
```

## Candidate Guidelines

- Prefer moving includes from headers to `.cpp` files when types are only needed for implementation.
- Prefer forward declarations or `src/rhi-shared-fwd.h` where ownership/layout does not require a complete type.
- Split narrow helpers from STL-heavy umbrella headers only when the measured fanout justifies it.
- Keep candidates small: one header family or include cluster per experiment.
- Avoid public API redesign unless the user explicitly asks for it.

## Validation

For candidate edits, compile directly affected TUs first with `probe`. For accepted batches, run:

```powershell
cmake --build build --config Debug --target slang-rhi
cmake --build build --config Debug --target slang-rhi-tests
```

In this repo, CMake builds must run outside the sandbox with escalation.

Smoke public headers when changing public includes:

```powershell
python tools/compile-research/compile_research.py --build-dir build --config Debug smoke-headers --json build/compile-research/smoke-headers.json
```

## Reporting

Summarize:

- candidate changed;
- baseline vs candidate median compile time;
- include count changes;
- accepted/rejected decision;
- validation commands run;
- paths to reports under `build/compile-research/`.

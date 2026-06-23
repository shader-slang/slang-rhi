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
3. Choose one coherent candidate with enough expected impact to rise above measurement noise. Prefer a header family or ownership boundary such as `src/core/common.h`, `src/rhi-shared.h`, a backend `*-base.h`, or a public header fanout issue. Do not limit the experiment to a one-line include tweak when the dependency problem requires coordinated direct includes, forward declarations, or moving implementation-only types out of a widely included header.
4. Capture a baseline probe for the affected TUs before editing. Use at least 7 measured iterations and optionally one warmup when timing matters:

```powershell
python tools/compile-research/compile_research.py --build-dir build --config Debug probe --header src/core/common.h --limit 15 --iterations 7 --warmups 1 --json build/compile-research/common-baseline.json
```

5. Apply one coherent include or header-boundary refactor. It may touch several files when all edits serve the same measured dependency reduction.
6. Run the same probe again and compare. The comparison includes aggregate median/mean timing, include-count deltas, and an accept/reject suggestion using a default 3% noise band:

```powershell
python tools/compile-research/compile_research.py compare build/compile-research/common-baseline.json build/compile-research/common-candidate.json --markdown build/compile-research/common-comparison.md --json build/compile-research/common-comparison.json
```

7. Decision rule:
   - keep when aggregate median compile time improves by at least 3%;
   - keep when aggregate include count drops by at least 25 without aggregate timing regression beyond 3%;
   - reject when aggregate median compile time regresses by more than 3%;
   - treat results inside the noise band as inconclusive and either rerun with more iterations or validate with repeated target builds.
8. For large candidates or noisy probe results, run repeated target builds. Touch the candidate header before each iteration to force a meaningful incremental rebuild, or use `--clean-first` for full rebuild timing. CMake builds must run outside the sandbox with escalation:

```powershell
python tools/compile-research/compile_research.py --build-dir build --config Debug build-timing --target slang-rhi --iterations 5 --warmups 1 --touch src/core/common.h --json build/compile-research/common-build-baseline.json
python tools/compile-research/compile_research.py compare-builds build/compile-research/common-build-baseline.json build/compile-research/common-build-candidate.json --markdown build/compile-research/common-build-comparison.md
```

9. Record the experiment:

```powershell
python tools/compile-research/compile_research.py record-experiment --name split-common-math --path src/core --baseline build/compile-research/common-baseline.json --candidate build/compile-research/common-candidate.json
```

## Candidate Guidelines

- Prefer moving includes from headers to `.cpp` files when types are only needed for implementation.
- Prefer forward declarations or `src/rhi-shared-fwd.h` where ownership/layout does not require a complete type.
- Split helpers, implementation-only state, or STL-heavy data members from umbrella headers when measured fanout justifies it.
- Prefer direct-include repairs as part of the same experiment when removing a transitive include exposes missing dependencies.
- Keep candidates coherent rather than tiny: one header family, backend base layer, or public fanout boundary per experiment.
- Avoid accepting changes based on single-TU wins. Use aggregate probe or build-timing results.
- Avoid public API redesign unless the user explicitly asks for it.

## Validation

For candidate edits, compile directly affected TUs first with `probe`. For accepted or high-impact batches, run repeated build timing if probe noise is close to the expected gain, then run normal target validation:

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
- aggregate baseline vs candidate median/mean compile time;
- aggregate include and unique include count changes;
- noise/variance notes when the decision is close;
- accepted/rejected decision;
- validation commands run;
- paths to reports under `build/compile-research/`.

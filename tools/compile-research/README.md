# Compile Research

This directory contains a small measurement harness for compile-time include research.
It is intentionally separate from the normal CMake build and does not use precompiled
headers or unity builds.

Generated data is written under `build/compile-research/`, which is already ignored by
the repository.

## Rank Current Costs

Use the existing MSVC Debug compile database and Ninja log:

```powershell
python tools/compile-research/compile_research.py --build-dir build --config Debug rank --top 25 --json build/compile-research/rank.json
```

The report includes:

- slow translation units from `build/.ninja_log`;
- local headers ranked by summed compile time of TUs that transitively include them;
- direct include fanout;
- STL headers used from project headers.

## Probe Translation Units

Measure isolated single-file compiles with rewritten object/PDB outputs and MSVC
`/showIncludes`:

```powershell
python tools/compile-research/compile_research.py --build-dir build --config Debug probe --top-slow 5 --iterations 3 --json build/compile-research/baseline-probe.json
```

Probe all TUs that transitively include a specific header:

```powershell
python tools/compile-research/compile_research.py --build-dir build --config Debug probe --header src/core/common.h --limit 10 --iterations 3 --json build/compile-research/common-baseline.json
```

The probe command removes `/MP` for the isolated compile, rewrites `/Fo` and `/Fd` into
the output directory, and stores stdout/stderr next to the JSON report.

## Compare Experiments

After applying a candidate include refactor, run the same probe again and compare:

```powershell
python tools/compile-research/compile_research.py compare build/compile-research/common-baseline.json build/compile-research/common-candidate.json --markdown build/compile-research/common-comparison.md
```

Negative delta values mean the candidate was faster.

## Record an Experiment

Store the current candidate diff, probe JSONs, and comparison under
`build/compile-research/experiments/<name>/`:

```powershell
python tools/compile-research/compile_research.py record-experiment --name split-common-math --path src/core --baseline build/compile-research/common-baseline.json --candidate build/compile-research/common-candidate.json
```

By default, this refuses to record if files outside `--path` are dirty. Use
`--allow-unrelated-dirty` only when unrelated local edits are intentional. Untracked
text files under `--path` are included in `candidate.diff`, and all untracked files
under `--path` are copied into the experiment bundle.

## Smoke Public Headers

Compile synthetic TUs for `slang-rhi.h`, `slang-rhi-device.h`, and extension headers:

```powershell
python tools/compile-research/compile_research.py --build-dir build --config Debug smoke-headers --json build/compile-research/smoke-headers.json
```

Some extension headers may depend on optional SDK headers. Treat failures there as
useful compatibility signals rather than automatic compile-time wins or losses.

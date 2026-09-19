"""Run identical binding tests on two executables, alternating order; preserve samples and compare medians.

Example (Release executables; shader fixtures must be present in both source trees):
    python tools/benchmark-bindings.py --before build/binding-matrix-base-build/Release/slang-rhi-tests.exe \
        --after build/Release/slang-rhi-tests.exe --output build/binding-matrix
"""

import argparse
import csv
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess


METRICS = ("setup", "encode", "finish", "submit", "retire", "cpu", "wall", "gpu")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--count", type=int, default=1024)
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--filter", default="benchmark-bindings-*")
    parser.add_argument("--affinity-mask", type=lambda value: int(value, 0), help="Optional Windows process CPU mask")
    parser.add_argument("--mode", type=int, choices=range(13), help="Isolate one mode (index in kModes in the C++ fixture)")
    args = parser.parse_args()
    if min(args.runs, args.count, args.samples) < 1:
        parser.error("runs, count, and samples must be positive")
    if args.affinity_mask is not None and (os.name != "nt" or args.affinity_mask <= 0):
        parser.error("affinity-mask requires Windows and a positive mask")
    args.output.mkdir(parents=True, exist_ok=True)
    executables = {"before": args.before.resolve(), "after": args.after.resolve()}
    metadata = {
        "platform": platform.platform(),
        "processor": platform.processor(),
        "runs": args.runs,
        "count": args.count,
        "samples": args.samples,
        "filter": args.filter,
        "mode": args.mode,
        "priority": "above normal" if os.name == "nt" else "inherited",
        "affinity": hex(args.affinity_mask) if args.affinity_mask is not None else "inherited",
        "executables": {
            version: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            for version, path in executables.items()
        },
    }
    (args.output / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")
    env = dict(os.environ)
    env["SLANG_RHI_BINDING_BENCHMARK_COUNT"] = str(args.count)
    env["SLANG_RHI_BINDING_BENCHMARK_SAMPLES"] = str(args.samples)
    if args.mode is not None:
        env["SLANG_RHI_BINDING_BENCHMARK_MODE"] = str(args.mode)
    else:
        env.pop("SLANG_RHI_BINDING_BENCHMARK_MODE", None)
    rows = []
    for run in range(args.runs):
        for version in (("before", "after") if run % 2 == 0 else ("after", "before")):
            log = args.output / f"{version}-{run + 1}.txt"
            print(f"Run {run + 1}/{args.runs}: {version}", flush=True)
            flags = subprocess.CREATE_NO_WINDOW | subprocess.ABOVE_NORMAL_PRIORITY_CLASS if os.name == "nt" else 0
            with log.open("w", encoding="utf-8") as stream:
                process = subprocess.Popen(
                    [str(executables[version]), "-tc=" + args.filter],
                    stdout=stream, stderr=subprocess.STDOUT, env=env, creationflags=flags,
                )
                if args.affinity_mask is not None:
                    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
                    kernel.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
                    kernel.SetProcessAffinityMask.restype = ctypes.c_int
                    if not kernel.SetProcessAffinityMask(int(process._handle), args.affinity_mask):
                        process.terminate()
                        process.wait()
                        raise ctypes.WinError(ctypes.get_last_error())
                code = process.wait()
            if code:
                raise RuntimeError(f"{version} exited {code}; see {log}. Invalid results are not compared.")
            parsed = 0
            for match in re.finditer(r"binding-matrix,([^\r\n]+)", log.read_text(encoding="utf-8")):
                fields = match.group(1).split(",")
                if len(fields) != 13:
                    raise RuntimeError(f"Malformed result in {log}: {match.group(0)}")
                workload, backend, mode, count, sample, *values = fields
                row = dict(version=version, run=run + 1, workload=workload, backend=backend,
                           mode=mode, count=int(count), sample=int(sample))
                row.update(zip(METRICS, map(float, values)))
                rows.append(row)
                parsed += 1
            if not parsed:
                raise RuntimeError(f"No benchmark samples found in {log}")
            print(f"  {parsed} checked samples", flush=True)
    with (args.output / "samples.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    keys = sorted({(r["workload"], r["backend"], r["mode"], r["count"]) for r in rows})
    summary = []
    for workload, backend, mode, count in keys:
        row = dict(workload=workload, backend=backend, mode=mode, count=count)
        for metric in METRICS:
            medians = {}
            for version in executables:
                run_medians = []
                for run in range(1, args.runs + 1):
                    values = [r[metric] for r in rows if r["workload"] == workload and r["backend"] == backend
                              and r["mode"] == mode and r["count"] == count and r["version"] == version and r["run"] == run]
                    if len(values) != args.samples:
                        raise RuntimeError(f"Incomplete samples for {version}/{workload}/{backend}/{mode}/{run}")
                    run_medians.append(statistics.median(values))
                medians[version] = statistics.median(run_medians)
                row[f"{version}_{metric}_ns"] = round(medians[version], 1)
                if metric in ("cpu", "encode", "gpu"):
                    row[f"{version}_{metric}_run_min_ns"] = round(min(run_medians), 1)
                    row[f"{version}_{metric}_run_max_ns"] = round(max(run_medians), 1)
            row[f"{metric}_speedup"] = round(medians["before"] / medians["after"], 3) if medians["after"] > 0 else ""
        summary.append(row)
    with (args.output / "comparison.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    print(f"Saved {len(summary)} comparisons to {args.output / 'comparison.csv'}", flush=True)


if __name__ == "__main__":
    main()

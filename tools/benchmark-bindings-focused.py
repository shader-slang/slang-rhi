"""Run each binding workload/mode in a separate process, using benchmark-bindings.py.

Pass exact doctest names with --cases (no wildcards). Fresh-object modes are excluded
by default; --modes can select any existing fixture modes explicitly.
"""

import argparse
import csv
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cases", nargs="+", required=True)
    parser.add_argument("--modes", nargs="+", type=int, choices=range(13), default=[2, 3, 4, 5, 6, 7, 10, 11, 12])
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--count", type=int, default=1024)
    parser.add_argument("--affinity-mask", type=lambda x: int(x, 0))
    args = parser.parse_args()
    if any(not case.startswith("benchmark-bindings-") or any(c in case for c in "*?,/\\") for case in args.cases):
        parser.error("cases must be exact benchmark-bindings-* test names, without wildcards or separators")
    if min(args.runs, args.samples, args.count) < 1:
        parser.error("runs, samples, and count must be positive")
    if len(set(args.cases)) != len(args.cases) or len(set(args.modes)) != len(args.modes):
        parser.error("duplicate cases or modes would overwrite results")
    args.output.mkdir(parents=True, exist_ok=True)
    rows, runs = [], []
    runner = Path(__file__).with_name("benchmark-bindings.py")
    for case in args.cases:
        for mode in args.modes:
            directory = args.output / f"{case}-mode-{mode}"
            command = [sys.executable, str(runner), "--before", str(args.before), "--after", str(args.after),
                       "--output", str(directory), "--filter", case, "--mode", str(mode),
                       "--runs", str(args.runs), "--samples", str(args.samples), "--count", str(args.count)]
            if args.affinity_mask is not None:
                command += ["--affinity-mask", hex(args.affinity_mask)]
            print(f"{case}, mode {mode}", flush=True)
            subprocess.run(command, check=True)
            with (directory / "comparison.csv").open(newline="") as stream:
                comparisons = list(csv.DictReader(stream))
            if len(comparisons) != 1:
                raise RuntimeError(f"Expected exactly one comparison in {directory}")
            rows.extend(comparisons)
            runs.append({"directory": directory.name, **json.loads((directory / "run.json").read_text())})
    with (args.output / "comparison.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    (args.output / "run.json").write_text(json.dumps(runs, indent=2) + "\n")
    print(f"Saved {len(rows)} isolated comparisons to {args.output}")


if __name__ == "__main__":
    main()

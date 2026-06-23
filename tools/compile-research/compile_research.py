#!/usr/bin/env python

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable


CPP_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".mm"}
SCAN_ROOTS = ("include", "src", "tests")
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
DEFAULT_PROBE_ITERATIONS = 7
DEFAULT_BUILD_ITERATIONS = 5
DEFAULT_NOISE_PCT = 3.0
DEFAULT_MIN_IMPROVEMENT_PCT = 3.0
DEFAULT_INCLUDE_DROP_THRESHOLD = 25


@dataclass(frozen=True)
class IncludeRef:
    raw: str
    delimiter: str
    resolved: str | None


@dataclass
class CompileEntry:
    file: Path
    rel_file: str
    directory: Path
    command: str
    config: str | None
    output: str | None
    duration_ms: int | None = None


@dataclass
class NinjaRow:
    start_ms: int
    end_ms: int
    output: str

    @property
    def duration_ms(self) -> int:
        return self.end_ms - self.start_ms


def repo_root_from(start: Path) -> Path:
    current = start.resolve()
    for path in (current, *current.parents):
        if (path / "CMakeLists.txt").exists() and (path / "include").exists() and (path / "src").exists():
            return path
    raise RuntimeError(f"could not find repository root from {start}")


def default_repo_root() -> Path:
    return repo_root_from(Path(__file__).resolve())


def normalize_slashes(value: str) -> str:
    return value.replace("\\", "/")


def repo_rel(path: Path, repo: Path) -> str:
    path = path.resolve()
    repo = repo.resolve()
    try:
        return path.relative_to(repo).as_posix()
    except ValueError:
        return path.as_posix()


def resolve_path(path: str | Path, base: Path) -> Path:
    p = Path(path)
    if p.is_absolute():
        return p
    return base / p


def stable_json_dump(data: object, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")


def read_json(path: Path) -> object:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def quote_msvc_path(path: Path) -> str:
    return '"' + str(path) + '"'


def sanitize_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_") or "item"


def as_float(value: object) -> float | None:
    if value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def format_number(value: object, suffix: str = "", signed: bool = False, digits: int = 1) -> str:
    number = as_float(value)
    if number is None:
        return "n/a"
    sign = "+" if signed else ""
    return f"{number:{sign}.{digits}f}{suffix}"


def format_int_delta(value: object) -> str:
    number = as_float(value)
    if number is None:
        return "n/a"
    return f"{number:+.0f}"


def summarize_numbers(values: list[float]) -> dict[str, object]:
    if not values:
        return {
            "count": 0,
            "min": None,
            "max": None,
            "mean": None,
            "median": None,
            "stdev": None,
            "relative_stdev_pct": None,
            "median_abs_deviation": None,
            "relative_mad_pct": None,
        }

    median = float(statistics.median(values))
    mean = float(statistics.mean(values))
    stdev = float(statistics.stdev(values)) if len(values) > 1 else 0.0
    mad = float(statistics.median([abs(value - median) for value in values])) if len(values) > 1 else 0.0
    return {
        "count": len(values),
        "min": float(min(values)),
        "max": float(max(values)),
        "mean": mean,
        "median": median,
        "stdev": stdev,
        "relative_stdev_pct": (stdev / mean * 100.0) if mean else None,
        "median_abs_deviation": mad,
        "relative_mad_pct": (mad / median * 100.0) if median else None,
    }


def summarize_milliseconds(values: list[float]) -> dict[str, object]:
    stats = summarize_numbers(values)
    return {
        "count": stats["count"],
        "min_ms": stats["min"],
        "max_ms": stats["max"],
        "mean_ms": stats["mean"],
        "median_ms": stats["median"],
        "stdev_ms": stats["stdev"],
        "relative_stdev_pct": stats["relative_stdev_pct"],
        "median_abs_deviation_ms": stats["median_abs_deviation"],
        "relative_mad_pct": stats["relative_mad_pct"],
    }


def pct_delta(before: object, after: object) -> float | None:
    before_float = as_float(before)
    after_float = as_float(after)
    if before_float is None or after_float is None or before_float == 0:
        return None
    return (after_float - before_float) / before_float * 100.0


def extract_config(command: str, output: str | None) -> str | None:
    match = re.search(r"CMAKE_INTDIR=\\?\"?([^\\\" ]+)", command)
    if match:
        return match.group(1)
    if output:
        parts = normalize_slashes(output).split("/")
        for part in parts:
            if part in {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"}:
                return part
    return None


def extract_output(command: str) -> str | None:
    match = re.search(r"(?:/Fo|-Fo)(\"[^\"]+\"|\S+)", command)
    if not match:
        return None
    value = match.group(1).strip('"')
    return normalize_slashes(value)


def replace_msvc_joined_arg(command: str, option: str, value: Path) -> str:
    pattern = re.compile(rf"({re.escape(option)})(\"[^\"]+\"|\S+)")
    if pattern.search(command):
        return pattern.sub(lambda match: match.group(1) + quote_msvc_path(value), command, count=1)
    return command + f" {option}{quote_msvc_path(value)}"


def replace_compile_source(command: str, source: Path) -> str:
    quoted = quote_msvc_path(source)
    pattern = re.compile(r"((?:-c|/c)\s+)(\"[^\"]+\"|\S+)\s*$")
    if pattern.search(command):
        return pattern.sub(lambda match: match.group(1) + quoted, command, count=1)
    return command + f" -c {quoted}"


def remove_option(command: str, option: str) -> str:
    return re.sub(rf"(?<!\S){re.escape(option)}(?!\S)", "", command)


def add_option_once(command: str, option: str) -> str:
    if re.search(rf"(?<!\S){re.escape(option)}(?!\S)", command):
        return command
    return command + f" {option}"


def load_compile_db(path: Path, repo: Path) -> list[CompileEntry]:
    raw = read_json(path)
    if not isinstance(raw, list):
        raise RuntimeError(f"{path} is not a compile_commands.json array")

    entries: list[CompileEntry] = []
    for item in raw:
        if not isinstance(item, dict) or "file" not in item:
            continue
        directory = resolve_path(item.get("directory", path.parent), repo).resolve()
        file_path = resolve_path(item["file"], directory).resolve()
        command = item.get("command")
        if not command and "arguments" in item:
            command = subprocess.list2cmdline(item["arguments"])
        if not command:
            continue
        output = item.get("output") or extract_output(command)
        output_norm = normalize_slashes(output) if output else None
        entries.append(
            CompileEntry(
                file=file_path,
                rel_file=repo_rel(file_path, repo),
                directory=directory,
                command=command,
                config=extract_config(command, output_norm),
                output=output_norm,
            )
        )
    return entries


def parse_ninja_log(path: Path) -> list[NinjaRow]:
    if not path.exists():
        return []
    rows: list[NinjaRow] = []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 4:
                continue
            try:
                start_ms = int(parts[0])
                end_ms = int(parts[1])
            except ValueError:
                continue
            rows.append(NinjaRow(start_ms=start_ms, end_ms=end_ms, output=normalize_slashes(parts[3])))
    return rows


def attach_ninja_times(entries: list[CompileEntry], rows: list[NinjaRow]) -> None:
    by_output: dict[str, NinjaRow] = {}
    for row in rows:
        by_output[row.output] = row

    rows_by_suffix = [row for row in rows if row.output.endswith((".obj", ".o"))]
    for entry in entries:
        duration: int | None = None
        if entry.output:
            output = normalize_slashes(entry.output)
            row = by_output.get(output)
            if not row and not Path(output).is_absolute():
                row = by_output.get(output.lstrip("./"))
            if row:
                duration = row.duration_ms
        if duration is None:
            suffix = entry.rel_file + ".obj"
            candidates = [row for row in rows_by_suffix if row.output.endswith(suffix)]
            if entry.config:
                config_token = "/" + entry.config + "/"
                config_candidates = [row for row in candidates if config_token in "/" + row.output]
                if config_candidates:
                    candidates = config_candidates
            if candidates:
                duration = candidates[-1].duration_ms
        entry.duration_ms = duration


def filter_entries(entries: list[CompileEntry], config: str | None) -> list[CompileEntry]:
    seen: set[str] = set()
    result: list[CompileEntry] = []
    for entry in entries:
        if config and entry.config and entry.config != config:
            continue
        if entry.rel_file in seen:
            continue
        seen.add(entry.rel_file)
        result.append(entry)
    return result


def scan_files(repo: Path, roots: Iterable[str] = SCAN_ROOTS) -> list[Path]:
    files: list[Path] = []
    for root_name in roots:
        root = repo / root_name
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix in CPP_EXTENSIONS:
                files.append(path)
    return files


def resolve_include(include: str, delimiter: str, includer: Path, repo: Path, include_roots: list[Path]) -> Path | None:
    candidates: list[Path] = []
    if delimiter == '"':
        candidates.append(includer.parent / include)
    for root in include_roots:
        candidates.append(root / include)
    for candidate in candidates:
        if candidate.exists() and candidate.is_file():
            return candidate.resolve()
    return None


def build_include_graph(repo: Path) -> dict[str, list[IncludeRef]]:
    include_roots = [repo / "include", repo / "src", repo / "tests"]
    graph: dict[str, list[IncludeRef]] = {}
    for path in scan_files(repo):
        rel = repo_rel(path, repo)
        refs: list[IncludeRef] = []
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            graph[rel] = refs
            continue
        for line in lines:
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            delimiter = match.group(1)
            include = match.group(2)
            resolved = resolve_include(include, delimiter, path, repo, include_roots)
            refs.append(
                IncludeRef(
                    raw=include,
                    delimiter=delimiter,
                    resolved=repo_rel(resolved, repo) if resolved else None,
                )
            )
        graph[rel] = refs
    return graph


def transitive_local_includes(graph: dict[str, list[IncludeRef]], rel_file: str) -> set[str]:
    result: set[str] = set()
    visiting: set[str] = set()

    def visit(path: str) -> None:
        if path in visiting:
            return
        visiting.add(path)
        for ref in graph.get(path, []):
            if not ref.resolved or ref.resolved in result:
                continue
            result.add(ref.resolved)
            visit(ref.resolved)
        visiting.remove(path)

    visit(rel_file)
    return result


def direct_include_counts(graph: dict[str, list[IncludeRef]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for refs in graph.values():
        for ref in refs:
            key = ref.resolved or ref.raw
            counts[key] = counts.get(key, 0) + 1
    return counts


def header_costs(entries: list[CompileEntry], graph: dict[str, list[IncludeRef]]) -> list[dict[str, object]]:
    costs: dict[str, int] = {}
    tu_counts: dict[str, int] = {}
    for entry in entries:
        if entry.duration_ms is None:
            continue
        for header in transitive_local_includes(graph, entry.rel_file):
            costs[header] = costs.get(header, 0) + entry.duration_ms
            tu_counts[header] = tu_counts.get(header, 0) + 1
    rows = [
        {
            "header": header,
            "cost_ms": cost,
            "translation_units": tu_counts[header],
        }
        for header, cost in costs.items()
    ]
    rows.sort(key=lambda row: (int(row["cost_ms"]), int(row["translation_units"])), reverse=True)
    return rows


def stl_header_counts(graph: dict[str, list[IncludeRef]]) -> list[dict[str, object]]:
    rows: dict[str, dict[str, object]] = {}
    for source, refs in graph.items():
        if not source.startswith(("include/", "src/")) or not Path(source).suffix in {".h", ".hpp", ".hh", ".hxx"}:
            continue
        for ref in refs:
            if ref.delimiter != "<":
                continue
            if "/" in ref.raw or "." in ref.raw:
                continue
            row = rows.setdefault(ref.raw, {"header": ref.raw, "count": 0, "including_files": []})
            row["count"] = int(row["count"]) + 1
            cast_files = row["including_files"]
            assert isinstance(cast_files, list)
            cast_files.append(source)
    result = list(rows.values())
    result.sort(key=lambda row: int(row["count"]), reverse=True)
    return result


def slow_translation_units(entries: list[CompileEntry]) -> list[dict[str, object]]:
    rows = [
        {
            "source": entry.rel_file,
            "duration_ms": entry.duration_ms,
            "object": entry.output,
        }
        for entry in entries
        if entry.duration_ms is not None
    ]
    rows.sort(key=lambda row: int(row["duration_ms"]), reverse=True)
    return rows


def print_table(title: str, rows: list[dict[str, object]], columns: list[tuple[str, str]], limit: int) -> None:
    print(title)
    print("=" * len(title))
    if not rows:
        print("(none)")
        print()
        return
    selected = rows[:limit]
    widths: dict[str, int] = {}
    for key, heading in columns:
        widths[key] = max(len(heading), *(len(str(row.get(key, ""))) for row in selected))
    print("  ".join(heading.ljust(widths[key]) for key, heading in columns))
    print("  ".join("-" * widths[key] for key, _ in columns))
    for row in selected:
        print("  ".join(str(row.get(key, "")).ljust(widths[key]) for key, _ in columns))
    print()


def command_rank(args: argparse.Namespace) -> int:
    repo = repo_root_from(Path(args.repo))
    build_dir = resolve_path(args.build_dir, repo)
    entries = load_compile_db(build_dir / "compile_commands.json", repo)
    rows = parse_ninja_log(build_dir / ".ninja_log")
    attach_ninja_times(entries, rows)
    entries = filter_entries(entries, args.config)
    graph = build_include_graph(repo)

    slow_tus = slow_translation_units(entries)
    costs = header_costs(entries, graph)
    direct_counts = direct_include_counts(graph)
    fanout = [
        {"include": include, "direct_count": count}
        for include, count in sorted(direct_counts.items(), key=lambda item: item[1], reverse=True)
    ]
    stl_counts = stl_header_counts(graph)

    report = {
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "repo": str(repo),
        "build_dir": str(build_dir),
        "config": args.config,
        "compile_entries": len(entries),
        "ninja_log_rows": len(rows),
        "slow_translation_units": slow_tus,
        "high_cost_headers": costs,
        "direct_include_fanout": fanout,
        "stl_headers_in_project_headers": stl_counts,
    }

    print_table(
        "Slow translation units",
        slow_tus,
        [("duration_ms", "ms"), ("source", "source")],
        args.top,
    )
    print_table(
        "High-cost local headers",
        costs,
        [("cost_ms", "cost ms"), ("translation_units", "TUs"), ("header", "header")],
        args.top,
    )
    print_table(
        "Direct include fanout",
        fanout,
        [("direct_count", "refs"), ("include", "include")],
        args.top,
    )
    print_table(
        "STL headers in project headers",
        stl_counts,
        [("count", "refs"), ("header", "header")],
        min(args.top, 20),
    )

    if args.json:
        stable_json_dump(report, Path(args.json))
        print(f"Wrote {args.json}")
    return 0


def select_entries(args: argparse.Namespace, repo: Path, entries: list[CompileEntry]) -> list[CompileEntry]:
    selected: dict[str, CompileEntry] = {}

    for source in args.source or []:
        source_norm = normalize_slashes(source)
        matches = [
            entry
            for entry in entries
            if entry.rel_file == source_norm or entry.rel_file.endswith(source_norm) or source_norm in entry.rel_file
        ]
        if not matches:
            raise RuntimeError(f"no compile command matched source {source}")
        for entry in matches:
            selected[entry.rel_file] = entry

    graph: dict[str, list[IncludeRef]] | None = None
    if args.header:
        graph = build_include_graph(repo)
        header_names = {normalize_slashes(header) for header in args.header}
        for entry in entries:
            includes = transitive_local_includes(graph, entry.rel_file)
            if any(header in includes or any(path.endswith(header) for path in includes) for header in header_names):
                selected[entry.rel_file] = entry

    if args.top_slow:
        ranked = [entry for entry in entries if entry.duration_ms is not None]
        ranked.sort(key=lambda entry: int(entry.duration_ms or 0), reverse=True)
        for entry in ranked[: args.top_slow]:
            selected[entry.rel_file] = entry

    result = list(selected.values())
    result.sort(key=lambda entry: int(entry.duration_ms or 0), reverse=True)
    if args.limit:
        result = result[: args.limit]
    if not result:
        raise RuntimeError("no translation units selected")
    return result


def parse_show_includes(output: str) -> list[str]:
    includes: list[str] = []
    for line in output.splitlines():
        lower = line.lower()
        marker = "including file:"
        index = lower.find(marker)
        if index < 0:
            continue
        includes.append(line[index + len(marker) :].strip())
    return includes


def run_compile_command(
    entry: CompileEntry,
    source: Path,
    out_dir: Path,
    label: str,
    include_tree: bool,
    timeout_seconds: int,
) -> dict[str, object]:
    out_dir.mkdir(parents=True, exist_ok=True)
    obj = out_dir / f"{label}.obj"
    pdb = out_dir / f"{label}.pdb"
    command = entry.command
    command = remove_option(command, "/MP")
    command = replace_msvc_joined_arg(command, "/Fo", obj)
    command = replace_msvc_joined_arg(command, "/Fd", pdb)
    command = replace_compile_source(command, source)
    if include_tree:
        command = add_option_once(command, "/showIncludes")

    start = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=entry.directory,
        shell=True,
        text=True,
        capture_output=True,
        timeout=timeout_seconds,
    )
    elapsed_ms = int(round((time.perf_counter() - start) * 1000))
    stdout = completed.stdout or ""
    stderr = completed.stderr or ""
    include_lines = parse_show_includes(stdout + "\n" + stderr)

    (out_dir / f"{label}.stdout.txt").write_text(stdout, encoding="utf-8", newline="\n")
    (out_dir / f"{label}.stderr.txt").write_text(stderr, encoding="utf-8", newline="\n")

    return {
        "label": label,
        "source": repo_rel(source, default_repo_root()) if source.is_absolute() else source.as_posix(),
        "elapsed_ms": elapsed_ms,
        "exit_code": completed.returncode,
        "object": str(obj),
        "pdb": str(pdb),
        "include_count": len(include_lines),
        "unique_include_count": len(set(include_lines)),
        "includes": include_lines,
        "command": command,
        "stdout": str(out_dir / f"{label}.stdout.txt"),
        "stderr": str(out_dir / f"{label}.stderr.txt"),
    }


def summarize_probe_runs(source: str, runs: list[dict[str, object]]) -> dict[str, object]:
    times = [float(run["elapsed_ms"]) for run in runs if int(run["exit_code"]) == 0]
    include_counts = [float(run["include_count"]) for run in runs if int(run["exit_code"]) == 0]
    unique_include_counts = [float(run["unique_include_count"]) for run in runs if int(run["exit_code"]) == 0]
    time_stats = summarize_milliseconds(times)
    return {
        "source": source,
        "runs": runs,
        "successful_runs": len(times),
        **time_stats,
        "median_include_count": statistics.median(include_counts) if include_counts else None,
        "median_unique_include_count": statistics.median(unique_include_counts) if unique_include_counts else None,
    }


def summarize_probe_set(probes: list[dict[str, object]]) -> dict[str, object]:
    successful = [
        probe
        for probe in probes
        if as_float(probe.get("median_ms")) is not None and int(probe.get("successful_runs", 0)) > 0
    ]
    median_times = [float(probe["median_ms"]) for probe in successful]
    mean_times = [float(probe["mean_ms"]) for probe in successful if as_float(probe.get("mean_ms")) is not None]
    include_counts = [
        float(probe["median_include_count"])
        for probe in successful
        if as_float(probe.get("median_include_count")) is not None
    ]
    unique_include_counts = [
        float(probe["median_unique_include_count"])
        for probe in successful
        if as_float(probe.get("median_unique_include_count")) is not None
    ]
    noise_values = [
        float(probe["relative_stdev_pct"])
        for probe in successful
        if as_float(probe.get("relative_stdev_pct")) is not None
    ]
    return {
        "translation_units": len(probes),
        "successful_translation_units": len(successful),
        "total_median_ms": sum(median_times) if median_times else None,
        "total_mean_ms": sum(mean_times) if mean_times else None,
        "total_median_include_count": sum(include_counts) if include_counts else None,
        "total_median_unique_include_count": sum(unique_include_counts) if unique_include_counts else None,
        "median_relative_stdev_pct": statistics.median(noise_values) if noise_values else None,
        "max_relative_stdev_pct": max(noise_values) if noise_values else None,
    }


def command_probe(args: argparse.Namespace) -> int:
    repo = repo_root_from(Path(args.repo))
    build_dir = resolve_path(args.build_dir, repo)
    entries = load_compile_db(build_dir / "compile_commands.json", repo)
    rows = parse_ninja_log(build_dir / ".ninja_log")
    attach_ninja_times(entries, rows)
    entries = filter_entries(entries, args.config)
    selected = select_entries(args, repo, entries)

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = resolve_path(args.out_dir, repo) if args.out_dir else build_dir / "compile-research" / "probes" / timestamp
    out_dir.mkdir(parents=True, exist_ok=True)

    probes: list[dict[str, object]] = []
    for entry in selected:
        warmup_runs: list[dict[str, object]] = []
        runs: list[dict[str, object]] = []
        source = entry.file
        source_label = sanitize_name(entry.rel_file)
        print(f"Probing {entry.rel_file}")
        for warmup in range(1, args.warmups + 1):
            label = f"{source_label}.warmup{warmup}"
            run = run_compile_command(
                entry=entry,
                source=source,
                out_dir=out_dir / source_label,
                label=label,
                include_tree=not args.no_show_includes,
                timeout_seconds=args.timeout,
            )
            warmup_runs.append(run)
            print(f"  warmup {warmup}: {run['elapsed_ms']} ms, exit {run['exit_code']}")
        for iteration in range(1, args.iterations + 1):
            label = f"{source_label}.run{iteration}"
            run = run_compile_command(
                entry=entry,
                source=source,
                out_dir=out_dir / source_label,
                label=label,
                include_tree=not args.no_show_includes,
                timeout_seconds=args.timeout,
            )
            runs.append(run)
            print(f"  run {iteration}: {run['elapsed_ms']} ms, exit {run['exit_code']}")
        probe = summarize_probe_runs(entry.rel_file, runs)
        if warmup_runs:
            probe["warmup_runs"] = warmup_runs
        probes.append(probe)

    report = {
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "repo": str(repo),
        "build_dir": str(build_dir),
        "config": args.config,
        "iterations": args.iterations,
        "warmups": args.warmups,
        "summary": summarize_probe_set(probes),
        "probes": probes,
    }
    report_path = Path(args.json) if args.json else out_dir / "probe-results.json"
    stable_json_dump(report, report_path)
    print(f"Wrote {report_path}")

    failed = []
    for probe in probes:
        all_runs = list(probe["runs"])
        all_runs.extend(probe.get("warmup_runs", []))
        if any(int(run["exit_code"]) != 0 for run in all_runs):
            failed.append(probe["source"])
    if failed:
        print("Some probes failed:")
        for source in failed:
            print(f"  {source}")
        return 1
    return 0


def public_headers(repo: Path) -> list[str]:
    headers = ["slang-rhi.h", "slang-rhi-device.h"]
    extension_root = repo / "include" / "slang-rhi"
    if extension_root.exists():
        for path in sorted(extension_root.glob("*.h")):
            headers.append("slang-rhi/" + path.name)
    return headers


def command_smoke_headers(args: argparse.Namespace) -> int:
    repo = repo_root_from(Path(args.repo))
    build_dir = resolve_path(args.build_dir, repo)
    entries = filter_entries(load_compile_db(build_dir / "compile_commands.json", repo), args.config)
    if not entries:
        raise RuntimeError("no compile commands available")
    template = entries[0]
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = resolve_path(args.out_dir, repo) if args.out_dir else build_dir / "compile-research" / "smoke-headers" / timestamp
    source_dir = out_dir / "sources"
    source_dir.mkdir(parents=True, exist_ok=True)

    headers = args.header or public_headers(repo)
    probes: list[dict[str, object]] = []
    for header in headers:
        source = source_dir / (sanitize_name(header) + ".cpp")
        source.write_text(f"#include <{header}>\nint main() {{ return 0; }}\n", encoding="utf-8", newline="\n")
        label = sanitize_name(header)
        print(f"Smoke compiling {header}")
        run = run_compile_command(
            entry=template,
            source=source,
            out_dir=out_dir / label,
            label=label,
            include_tree=not args.no_show_includes,
            timeout_seconds=args.timeout,
        )
        probes.append(
            {
                "header": header,
                "source": str(source),
                "exit_code": run["exit_code"],
                "elapsed_ms": run["elapsed_ms"],
                "include_count": run["include_count"],
                "unique_include_count": run["unique_include_count"],
                "stdout": run["stdout"],
                "stderr": run["stderr"],
            }
        )
        print(f"  {run['elapsed_ms']} ms, exit {run['exit_code']}")

    report = {
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "repo": str(repo),
        "build_dir": str(build_dir),
        "config": args.config,
        "headers": probes,
    }
    report_path = Path(args.json) if args.json else out_dir / "smoke-header-results.json"
    stable_json_dump(report, report_path)
    print(f"Wrote {report_path}")
    return 0 if all(int(row["exit_code"]) == 0 for row in probes) else 1


def probe_rows(path: Path) -> dict[str, dict[str, object]]:
    data = read_json(path)
    if not isinstance(data, dict):
        raise RuntimeError(f"{path} is not a probe report")
    rows: dict[str, dict[str, object]] = {}
    for probe in data.get("probes", []):
        if isinstance(probe, dict) and "source" in probe:
            rows[str(probe["source"])] = probe
    return rows


def compare_probe_files(baseline: Path, candidate: Path) -> list[dict[str, object]]:
    baseline_rows = probe_rows(baseline)
    candidate_rows = probe_rows(candidate)
    rows: list[dict[str, object]] = []
    for source in sorted(set(baseline_rows) | set(candidate_rows)):
        baseline_row = baseline_rows.get(source, {})
        candidate_row = candidate_rows.get(source, {})
        before = baseline_row.get("median_ms")
        after = candidate_row.get("median_ms")
        before_mean = baseline_row.get("mean_ms")
        after_mean = candidate_row.get("mean_ms")
        before_includes = baseline_row.get("median_include_count")
        after_includes = candidate_row.get("median_include_count")
        before_unique_includes = baseline_row.get("median_unique_include_count")
        after_unique_includes = candidate_row.get("median_unique_include_count")
        delta = None
        if as_float(before) is not None and as_float(after) is not None:
            delta = float(after) - float(before)
        rows.append(
            {
                "source": source,
                "baseline_ms": before,
                "candidate_ms": after,
                "baseline_mean_ms": before_mean,
                "candidate_mean_ms": after_mean,
                "delta_ms": delta,
                "delta_pct": pct_delta(before, after),
                "baseline_relative_stdev_pct": baseline_row.get("relative_stdev_pct"),
                "candidate_relative_stdev_pct": candidate_row.get("relative_stdev_pct"),
                "baseline_successful_runs": baseline_row.get("successful_runs"),
                "candidate_successful_runs": candidate_row.get("successful_runs"),
                "baseline_include_count": before_includes,
                "candidate_include_count": after_includes,
                "delta_include_count": (
                    float(after_includes) - float(before_includes)
                    if as_float(before_includes) is not None and as_float(after_includes) is not None
                    else None
                ),
                "baseline_unique_include_count": before_unique_includes,
                "candidate_unique_include_count": after_unique_includes,
                "delta_unique_include_count": (
                    float(after_unique_includes) - float(before_unique_includes)
                    if as_float(before_unique_includes) is not None and as_float(after_unique_includes) is not None
                    else None
                ),
            }
        )
    rows.sort(key=lambda row: float(row["delta_ms"] or 0.0))
    return rows


def summarize_probe_comparison(
    rows: list[dict[str, object]],
    noise_pct: float = DEFAULT_NOISE_PCT,
    min_improvement_pct: float = DEFAULT_MIN_IMPROVEMENT_PCT,
    include_drop_threshold: int = DEFAULT_INCLUDE_DROP_THRESHOLD,
) -> dict[str, object]:
    paired = [
        row
        for row in rows
        if as_float(row.get("baseline_ms")) is not None and as_float(row.get("candidate_ms")) is not None
    ]
    mean_paired = [
        row
        for row in paired
        if as_float(row.get("baseline_mean_ms")) is not None and as_float(row.get("candidate_mean_ms")) is not None
    ]
    total_baseline = sum(float(row["baseline_ms"]) for row in paired)
    total_candidate = sum(float(row["candidate_ms"]) for row in paired)
    total_baseline_mean = sum(float(row["baseline_mean_ms"]) for row in mean_paired)
    total_candidate_mean = sum(float(row["candidate_mean_ms"]) for row in mean_paired)
    total_include_delta = sum(
        float(row["delta_include_count"])
        for row in paired
        if as_float(row.get("delta_include_count")) is not None
    )
    total_unique_include_delta = sum(
        float(row["delta_unique_include_count"])
        for row in paired
        if as_float(row.get("delta_unique_include_count")) is not None
    )
    total_include_baseline = sum(
        float(row["baseline_include_count"])
        for row in paired
        if as_float(row.get("baseline_include_count")) is not None
    )
    total_include_candidate = sum(
        float(row["candidate_include_count"])
        for row in paired
        if as_float(row.get("candidate_include_count")) is not None
    )
    total_unique_include_baseline = sum(
        float(row["baseline_unique_include_count"])
        for row in paired
        if as_float(row.get("baseline_unique_include_count")) is not None
    )
    total_unique_include_candidate = sum(
        float(row["candidate_unique_include_count"])
        for row in paired
        if as_float(row.get("candidate_unique_include_count")) is not None
    )
    delta_ms = total_candidate - total_baseline if paired else None
    delta_pct = (delta_ms / total_baseline * 100.0) if paired and total_baseline else None
    mean_delta_ms = total_candidate_mean - total_baseline_mean if mean_paired else None
    mean_delta_pct = (mean_delta_ms / total_baseline_mean * 100.0) if mean_delta_ms is not None and total_baseline_mean else None
    missing_sources = [
        str(row["source"])
        for row in rows
        if as_float(row.get("baseline_ms")) is None or as_float(row.get("candidate_ms")) is None
    ]
    improved = [row for row in paired if as_float(row.get("delta_ms")) is not None and float(row["delta_ms"]) < 0]
    regressed = [row for row in paired if as_float(row.get("delta_ms")) is not None and float(row["delta_ms"]) > 0]

    decision = "incomplete"
    reason = "no paired successful probe rows"
    if paired and missing_sources:
        decision = "incomplete"
        reason = "one or more sources are missing from a report"
    elif paired and delta_pct is not None and delta_pct <= -min_improvement_pct:
        decision = "accept-timing"
        reason = f"aggregate median improved by at least {min_improvement_pct:.1f}%"
    elif (
        paired
        and delta_pct is not None
        and delta_pct <= noise_pct
        and (total_include_delta <= -include_drop_threshold or total_unique_include_delta <= -include_drop_threshold)
    ):
        decision = "accept-include-drop"
        reason = f"include count dropped by at least {include_drop_threshold} without an aggregate timing regression beyond {noise_pct:.1f}%"
    elif paired and delta_pct is not None and delta_pct > noise_pct:
        decision = "reject-regression"
        reason = f"aggregate median regressed by more than {noise_pct:.1f}%"
    elif paired:
        decision = "inconclusive-noise"
        reason = f"aggregate median delta is within +/-{noise_pct:.1f}% and include-count movement is below threshold"

    return {
        "paired_sources": len(paired),
        "missing_sources": missing_sources,
        "improved_sources": len(improved),
        "regressed_sources": len(regressed),
        "baseline_total_median_ms": total_baseline if paired else None,
        "candidate_total_median_ms": total_candidate if paired else None,
        "total_delta_ms": delta_ms,
        "total_delta_pct": delta_pct,
        "baseline_total_mean_ms": total_baseline_mean if mean_paired else None,
        "candidate_total_mean_ms": total_candidate_mean if mean_paired else None,
        "mean_delta_ms": mean_delta_ms,
        "mean_delta_pct": mean_delta_pct,
        "baseline_total_include_count": total_include_baseline if paired else None,
        "candidate_total_include_count": total_include_candidate if paired else None,
        "total_include_delta": total_include_delta if paired else None,
        "baseline_total_unique_include_count": total_unique_include_baseline if paired else None,
        "candidate_total_unique_include_count": total_unique_include_candidate if paired else None,
        "total_unique_include_delta": total_unique_include_delta if paired else None,
        "noise_threshold_pct": noise_pct,
        "min_improvement_pct": min_improvement_pct,
        "include_drop_threshold": include_drop_threshold,
        "decision": decision,
        "decision_reason": reason,
    }


def format_compare_markdown(rows: list[dict[str, object]], summary: dict[str, object] | None = None) -> str:
    lines: list[str] = []
    if summary:
        lines.extend(
            [
                "Aggregate summary",
                "=================",
                "",
                "| Metric | Baseline | Candidate | Delta |",
                "| --- | ---: | ---: | ---: |",
                "| Total median compile time | {baseline} | {candidate} | {delta} ({pct}) |".format(
                    baseline=format_number(summary.get("baseline_total_median_ms"), " ms"),
                    candidate=format_number(summary.get("candidate_total_median_ms"), " ms"),
                    delta=format_number(summary.get("total_delta_ms"), " ms", signed=True),
                    pct=format_number(summary.get("total_delta_pct"), "%", signed=True),
                ),
                "| Total mean compile time | {baseline} | {candidate} | {delta} ({pct}) |".format(
                    baseline=format_number(summary.get("baseline_total_mean_ms"), " ms"),
                    candidate=format_number(summary.get("candidate_total_mean_ms"), " ms"),
                    delta=format_number(summary.get("mean_delta_ms"), " ms", signed=True),
                    pct=format_number(summary.get("mean_delta_pct"), "%", signed=True),
                ),
                "| Median include count | {baseline} | {candidate} | {delta} |".format(
                    baseline=format_number(summary.get("baseline_total_include_count"), digits=0),
                    candidate=format_number(summary.get("candidate_total_include_count"), digits=0),
                    delta=format_int_delta(summary.get("total_include_delta")),
                ),
                "| Unique include count | {baseline} | {candidate} | {delta} |".format(
                    baseline=format_number(summary.get("baseline_total_unique_include_count"), digits=0),
                    candidate=format_number(summary.get("candidate_total_unique_include_count"), digits=0),
                    delta=format_int_delta(summary.get("total_unique_include_delta")),
                ),
                "",
                f"Decision: **{summary.get('decision')}** - {summary.get('decision_reason')}",
                "",
            ]
        )

    lines.extend(
        [
            "Per-source comparison",
            "=====================",
            "",
            "| Source | Baseline ms | Candidate ms | Delta ms | Delta % | Includes | Unique includes |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for row in rows:
        lines.append(
            "| {source} | {baseline} | {candidate} | {delta} | {pct} | {includes} | {unique_includes} |".format(
                source=row["source"],
                baseline=format_number(row.get("baseline_ms")),
                candidate=format_number(row.get("candidate_ms")),
                delta=format_number(row.get("delta_ms"), signed=True),
                pct=format_number(row.get("delta_pct"), "%", signed=True),
                includes=format_int_delta(row.get("delta_include_count")),
                unique_includes=format_int_delta(row.get("delta_unique_include_count")),
            )
        )
    return "\n".join(lines) + "\n"


def command_compare(args: argparse.Namespace) -> int:
    rows = compare_probe_files(Path(args.baseline), Path(args.candidate))
    summary = summarize_probe_comparison(
        rows,
        noise_pct=args.noise_pct,
        min_improvement_pct=args.min_improvement_pct,
        include_drop_threshold=args.include_drop_threshold,
    )
    markdown = format_compare_markdown(rows, summary)
    print(markdown)
    if args.json:
        stable_json_dump({"summary": summary, "comparisons": rows}, Path(args.json))
        print(f"Wrote {args.json}")
    if args.markdown:
        Path(args.markdown).parent.mkdir(parents=True, exist_ok=True)
        Path(args.markdown).write_text(markdown, encoding="utf-8", newline="\n")
        print(f"Wrote {args.markdown}")
    return 0


def build_command(build_dir: Path, config: str | None, targets: list[str] | None, clean_first: bool) -> list[str]:
    command = ["cmake", "--build", str(build_dir)]
    if config:
        command.extend(["--config", config])
    if targets:
        command.append("--target")
        command.extend(targets)
    if clean_first:
        command.append("--clean-first")
    return command


def touch_paths(repo: Path, paths: list[str] | None) -> list[str]:
    touched: list[str] = []
    for path in paths or []:
        resolved = resolve_path(path, repo)
        if not resolved.exists():
            raise RuntimeError(f"cannot touch missing path {path}")
        now = time.time()
        os.utime(resolved, (now, now))
        touched.append(repo_rel(resolved, repo))
    return touched


def run_build_iteration(
    command: list[str],
    repo: Path,
    out_dir: Path,
    label: str,
    timeout_seconds: int,
    touch: list[str] | None,
) -> dict[str, object]:
    out_dir.mkdir(parents=True, exist_ok=True)
    touched = touch_paths(repo, touch)
    start = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=repo,
        text=True,
        capture_output=True,
        timeout=timeout_seconds,
    )
    elapsed_ms = int(round((time.perf_counter() - start) * 1000))
    stdout_path = out_dir / f"{label}.stdout.txt"
    stderr_path = out_dir / f"{label}.stderr.txt"
    stdout_path.write_text(completed.stdout or "", encoding="utf-8", newline="\n")
    stderr_path.write_text(completed.stderr or "", encoding="utf-8", newline="\n")
    return {
        "label": label,
        "elapsed_ms": elapsed_ms,
        "exit_code": completed.returncode,
        "command": command,
        "touched_paths": touched,
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
    }


def summarize_timed_runs(runs: list[dict[str, object]]) -> dict[str, object]:
    times = [float(run["elapsed_ms"]) for run in runs if int(run["exit_code"]) == 0]
    stats = summarize_milliseconds(times)
    return {
        "runs": len(runs),
        "successful_runs": len(times),
        **stats,
    }


def command_build_timing(args: argparse.Namespace) -> int:
    repo = repo_root_from(Path(args.repo))
    build_dir = resolve_path(args.build_dir, repo)
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = resolve_path(args.out_dir, repo) if args.out_dir else build_dir / "compile-research" / "build-timing" / timestamp
    out_dir.mkdir(parents=True, exist_ok=True)

    command = build_command(build_dir, args.config, args.target, args.clean_first)
    warmup_runs: list[dict[str, object]] = []
    runs: list[dict[str, object]] = []
    print("Build command:")
    print("  " + subprocess.list2cmdline(command))
    for warmup in range(1, args.warmups + 1):
        label = f"warmup{warmup}"
        run = run_build_iteration(command, repo, out_dir, label, args.timeout, args.touch)
        warmup_runs.append(run)
        print(f"  warmup {warmup}: {run['elapsed_ms']} ms, exit {run['exit_code']}")
    for iteration in range(1, args.iterations + 1):
        label = f"run{iteration}"
        run = run_build_iteration(command, repo, out_dir, label, args.timeout, args.touch)
        runs.append(run)
        print(f"  run {iteration}: {run['elapsed_ms']} ms, exit {run['exit_code']}")

    report = {
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "repo": str(repo),
        "build_dir": str(build_dir),
        "config": args.config,
        "targets": args.target or [],
        "clean_first": args.clean_first,
        "touch": args.touch or [],
        "iterations": args.iterations,
        "warmups": args.warmups,
        "command": command,
        "summary": summarize_timed_runs(runs),
        "warmup_runs": warmup_runs,
        "runs": runs,
    }
    report_path = Path(args.json) if args.json else out_dir / "build-timing.json"
    stable_json_dump(report, report_path)
    print(f"Wrote {report_path}")
    all_runs = [*warmup_runs, *runs]
    return 0 if all(int(run["exit_code"]) == 0 for run in all_runs) else 1


def compare_build_timing_files(
    baseline: Path,
    candidate: Path,
    noise_pct: float = DEFAULT_NOISE_PCT,
    min_improvement_pct: float = DEFAULT_MIN_IMPROVEMENT_PCT,
) -> dict[str, object]:
    baseline_report = read_json(baseline)
    candidate_report = read_json(candidate)
    if not isinstance(baseline_report, dict) or not isinstance(candidate_report, dict):
        raise RuntimeError("build timing reports must be JSON objects")
    baseline_summary = baseline_report.get("summary", {})
    candidate_summary = candidate_report.get("summary", {})
    if not isinstance(baseline_summary, dict) or not isinstance(candidate_summary, dict):
        raise RuntimeError("build timing reports are missing summary objects")

    baseline_median = baseline_summary.get("median_ms")
    candidate_median = candidate_summary.get("median_ms")
    baseline_mean = baseline_summary.get("mean_ms")
    candidate_mean = candidate_summary.get("mean_ms")
    median_delta = (
        float(candidate_median) - float(baseline_median)
        if as_float(baseline_median) is not None and as_float(candidate_median) is not None
        else None
    )
    mean_delta = (
        float(candidate_mean) - float(baseline_mean)
        if as_float(baseline_mean) is not None and as_float(candidate_mean) is not None
        else None
    )
    median_delta_pct = pct_delta(baseline_median, candidate_median)
    mean_delta_pct = pct_delta(baseline_mean, candidate_mean)

    decision = "incomplete"
    reason = "one or both reports are missing successful build runs"
    if median_delta_pct is not None and median_delta_pct <= -min_improvement_pct:
        decision = "accept-timing"
        reason = f"median build time improved by at least {min_improvement_pct:.1f}%"
    elif median_delta_pct is not None and median_delta_pct > noise_pct:
        decision = "reject-regression"
        reason = f"median build time regressed by more than {noise_pct:.1f}%"
    elif median_delta_pct is not None:
        decision = "inconclusive-noise"
        reason = f"median build delta is within +/-{noise_pct:.1f}%"

    return {
        "baseline": str(baseline),
        "candidate": str(candidate),
        "baseline_median_ms": baseline_median,
        "candidate_median_ms": candidate_median,
        "median_delta_ms": median_delta,
        "median_delta_pct": median_delta_pct,
        "baseline_mean_ms": baseline_mean,
        "candidate_mean_ms": candidate_mean,
        "mean_delta_ms": mean_delta,
        "mean_delta_pct": mean_delta_pct,
        "baseline_relative_stdev_pct": baseline_summary.get("relative_stdev_pct"),
        "candidate_relative_stdev_pct": candidate_summary.get("relative_stdev_pct"),
        "baseline_successful_runs": baseline_summary.get("successful_runs"),
        "candidate_successful_runs": candidate_summary.get("successful_runs"),
        "noise_threshold_pct": noise_pct,
        "min_improvement_pct": min_improvement_pct,
        "decision": decision,
        "decision_reason": reason,
    }


def format_build_compare_markdown(summary: dict[str, object]) -> str:
    return "\n".join(
        [
            "Build timing comparison",
            "=======================",
            "",
            "| Metric | Baseline | Candidate | Delta |",
            "| --- | ---: | ---: | ---: |",
            "| Median build time | {baseline} | {candidate} | {delta} ({pct}) |".format(
                baseline=format_number(summary.get("baseline_median_ms"), " ms"),
                candidate=format_number(summary.get("candidate_median_ms"), " ms"),
                delta=format_number(summary.get("median_delta_ms"), " ms", signed=True),
                pct=format_number(summary.get("median_delta_pct"), "%", signed=True),
            ),
            "| Mean build time | {baseline} | {candidate} | {delta} ({pct}) |".format(
                baseline=format_number(summary.get("baseline_mean_ms"), " ms"),
                candidate=format_number(summary.get("candidate_mean_ms"), " ms"),
                delta=format_number(summary.get("mean_delta_ms"), " ms", signed=True),
                pct=format_number(summary.get("mean_delta_pct"), "%", signed=True),
            ),
            "| Relative stdev | {baseline} | {candidate} |  |".format(
                baseline=format_number(summary.get("baseline_relative_stdev_pct"), "%"),
                candidate=format_number(summary.get("candidate_relative_stdev_pct"), "%"),
            ),
            "",
            f"Decision: **{summary.get('decision')}** - {summary.get('decision_reason')}",
            "",
        ]
    )


def command_compare_builds(args: argparse.Namespace) -> int:
    summary = compare_build_timing_files(
        Path(args.baseline),
        Path(args.candidate),
        noise_pct=args.noise_pct,
        min_improvement_pct=args.min_improvement_pct,
    )
    markdown = format_build_compare_markdown(summary)
    print(markdown)
    if args.json:
        stable_json_dump({"summary": summary}, Path(args.json))
        print(f"Wrote {args.json}")
    if args.markdown:
        Path(args.markdown).parent.mkdir(parents=True, exist_ok=True)
        Path(args.markdown).write_text(markdown, encoding="utf-8", newline="\n")
        print(f"Wrote {args.markdown}")
    return 0


def git_lines(repo: Path, args: list[str]) -> list[str]:
    result = subprocess.run(
        ["git", *args],
        cwd=repo,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "git command failed")
    return [line for line in result.stdout.splitlines() if line.strip()]


def dirty_paths(repo: Path) -> set[str]:
    paths: set[str] = set()
    for line in git_lines(repo, ["status", "--porcelain"]):
        if len(line) < 4:
            continue
        path = line[3:]
        if " -> " in path:
            path = path.split(" -> ", 1)[1]
        paths.add(normalize_slashes(path))
    return paths


def is_under_any(path: str, allowed: set[str]) -> bool:
    if not allowed:
        return True
    path = normalize_slashes(path)
    for root in allowed:
        root = normalize_slashes(root).rstrip("/")
        if path == root or path.startswith(root + "/"):
            return True
    return False


def git_diff(repo: Path, paths: list[str], cached: bool) -> str:
    cmd = ["git", "diff", "--binary"]
    if cached:
        cmd.append("--cached")
    if paths:
        cmd.append("--")
        cmd.extend(paths)
    result = subprocess.run(cmd, cwd=repo, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "git diff failed")
    return result.stdout


def git_untracked(repo: Path, paths: list[str]) -> list[str]:
    cmd = ["git", "ls-files", "--others", "--exclude-standard"]
    if paths:
        cmd.append("--")
        cmd.extend(paths)
    result = subprocess.run(cmd, cwd=repo, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "git ls-files failed")
    return [normalize_slashes(line) for line in result.stdout.splitlines() if line.strip()]


def patch_for_untracked_text_file(repo: Path, rel_path: str) -> str | None:
    path = repo / rel_path
    data = path.read_bytes()
    if b"\0" in data:
        return None
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return None

    lines = text.splitlines(keepends=True)
    patch_lines = [
        f"diff --git a/{rel_path} b/{rel_path}\n",
        "new file mode 100644\n",
        "index 0000000..0000000\n",
        "--- /dev/null\n",
        f"+++ b/{rel_path}\n",
        f"@@ -0,0 +1,{len(lines)} @@\n",
    ]
    for line in lines:
        if line.endswith("\n"):
            patch_lines.append("+" + line)
        else:
            patch_lines.append("+" + line + "\n")
            patch_lines.append("\\ No newline at end of file\n")
    return "".join(patch_lines)


def command_record_experiment(args: argparse.Namespace) -> int:
    repo = repo_root_from(Path(args.repo))
    allowed = {normalize_slashes(path) for path in args.path}
    dirty = dirty_paths(repo)
    unrelated = sorted(path for path in dirty if not is_under_any(path, allowed))
    if unrelated and not args.allow_unrelated_dirty:
        print("Refusing to record experiment with unrelated dirty files:", file=sys.stderr)
        for path in unrelated:
            print(f"  {path}", file=sys.stderr)
        print("Pass --allow-unrelated-dirty to override.", file=sys.stderr)
        return 2

    build_dir = resolve_path(args.build_dir, repo)
    out_dir = build_dir / "compile-research" / "experiments" / sanitize_name(args.name)
    out_dir.mkdir(parents=True, exist_ok=True)

    diff = git_diff(repo, args.path, cached=False)
    untracked = git_untracked(repo, args.path)
    untracked_dir = out_dir / "untracked"
    copied_untracked: list[str] = []
    for rel_path in untracked:
        patch = patch_for_untracked_text_file(repo, rel_path)
        if patch is not None:
            diff += ("\n" if diff and not diff.endswith("\n") else "") + patch
        dst = untracked_dir / rel_path
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / rel_path, dst)
        copied_untracked.append(str(dst))

    staged_diff = git_diff(repo, args.path, cached=True)
    (out_dir / "candidate.diff").write_text(diff, encoding="utf-8", newline="\n")
    (out_dir / "candidate-staged.diff").write_text(staged_diff, encoding="utf-8", newline="\n")

    report: dict[str, object] = {
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "name": args.name,
        "paths": args.path,
        "dirty_paths": sorted(dirty),
        "untracked_paths": untracked,
        "copied_untracked_files": copied_untracked,
        "candidate_diff": str(out_dir / "candidate.diff"),
        "candidate_staged_diff": str(out_dir / "candidate-staged.diff"),
    }
    if args.baseline and args.candidate:
        comparisons = compare_probe_files(Path(args.baseline), Path(args.candidate))
        comparison_summary = summarize_probe_comparison(comparisons)
        report["comparison_summary"] = comparison_summary
        report["comparisons"] = comparisons
        (out_dir / "comparison.md").write_text(
            format_compare_markdown(comparisons, comparison_summary),
            encoding="utf-8",
            newline="\n",
        )
        shutil.copyfile(args.baseline, out_dir / "baseline-probe.json")
        shutil.copyfile(args.candidate, out_dir / "candidate-probe.json")
    if args.baseline_build and args.candidate_build:
        build_comparison = compare_build_timing_files(Path(args.baseline_build), Path(args.candidate_build))
        report["build_timing_comparison"] = build_comparison
        (out_dir / "build-comparison.md").write_text(
            format_build_compare_markdown(build_comparison),
            encoding="utf-8",
            newline="\n",
        )
        shutil.copyfile(args.baseline_build, out_dir / "baseline-build-timing.json")
        shutil.copyfile(args.candidate_build, out_dir / "candidate-build-timing.json")
    stable_json_dump(report, out_dir / "experiment.json")
    print(f"Wrote {out_dir}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Measure and rank compile-time include costs for slang-rhi.",
    )
    parser.add_argument("--repo", default=str(default_repo_root()), help="repository root")
    parser.add_argument("--build-dir", default="build", help="build directory containing compile_commands.json")
    parser.add_argument("--config", default="Debug", help="configuration to read from a multi-config build")

    subparsers = parser.add_subparsers(dest="command", required=True)

    rank = subparsers.add_parser("rank", help="rank slow translation units and high-cost headers")
    rank.add_argument("--top", type=int, default=25, help="number of rows to print")
    rank.add_argument("--json", help="write a JSON report")
    rank.set_defaults(func=command_rank)

    probe = subparsers.add_parser("probe", help="compile selected translation units with /showIncludes")
    probe.add_argument("--source", action="append", help="source path or substring to probe")
    probe.add_argument("--header", action="append", help="probe TUs that transitively include this header")
    probe.add_argument("--top-slow", type=int, default=0, help="also probe the N slowest TUs")
    probe.add_argument("--limit", type=int, default=0, help="maximum selected TUs")
    probe.add_argument("--iterations", type=int, default=DEFAULT_PROBE_ITERATIONS, help="number of measured compiles per TU")
    probe.add_argument("--warmups", type=int, default=0, help="warmup compiles per TU, excluded from statistics")
    probe.add_argument("--timeout", type=int, default=120, help="timeout per compile, in seconds")
    probe.add_argument("--out-dir", help="output directory, defaults under build/compile-research/probes")
    probe.add_argument("--json", help="write JSON report to this path")
    probe.add_argument("--no-show-includes", action="store_true", help="skip /showIncludes")
    probe.set_defaults(func=command_probe)

    build_timing = subparsers.add_parser("build-timing", help="run cmake builds repeatedly and summarize timings")
    build_timing.add_argument("--target", action="append", help="target to build; may be repeated")
    build_timing.add_argument("--iterations", type=int, default=DEFAULT_BUILD_ITERATIONS, help="number of measured builds")
    build_timing.add_argument("--warmups", type=int, default=0, help="warmup builds, excluded from statistics")
    build_timing.add_argument("--touch", action="append", help="path to touch before each build to force an incremental rebuild")
    build_timing.add_argument("--clean-first", action="store_true", help="pass --clean-first to cmake --build each run")
    build_timing.add_argument("--timeout", type=int, default=1800, help="timeout per build, in seconds")
    build_timing.add_argument("--out-dir", help="output directory, defaults under build/compile-research/build-timing")
    build_timing.add_argument("--json", help="write JSON report to this path")
    build_timing.set_defaults(func=command_build_timing)

    smoke = subparsers.add_parser("smoke-headers", help="compile synthetic TUs for public headers")
    smoke.add_argument("--header", action="append", help="specific public header to smoke compile")
    smoke.add_argument("--timeout", type=int, default=120, help="timeout per compile, in seconds")
    smoke.add_argument("--out-dir", help="output directory, defaults under build/compile-research/smoke-headers")
    smoke.add_argument("--json", help="write JSON report to this path")
    smoke.add_argument("--no-show-includes", action="store_true", help="skip /showIncludes")
    smoke.set_defaults(func=command_smoke_headers)

    compare = subparsers.add_parser("compare", help="compare two probe JSON reports")
    compare.add_argument("baseline")
    compare.add_argument("candidate")
    compare.add_argument("--noise-pct", type=float, default=DEFAULT_NOISE_PCT, help="aggregate timing noise band")
    compare.add_argument(
        "--min-improvement-pct",
        type=float,
        default=DEFAULT_MIN_IMPROVEMENT_PCT,
        help="minimum aggregate timing improvement to accept on timing alone",
    )
    compare.add_argument(
        "--include-drop-threshold",
        type=int,
        default=DEFAULT_INCLUDE_DROP_THRESHOLD,
        help="minimum aggregate include-count drop to accept within timing noise",
    )
    compare.add_argument("--json", help="write JSON comparison")
    compare.add_argument("--markdown", help="write markdown comparison")
    compare.set_defaults(func=command_compare)

    compare_builds = subparsers.add_parser("compare-builds", help="compare two build-timing JSON reports")
    compare_builds.add_argument("baseline")
    compare_builds.add_argument("candidate")
    compare_builds.add_argument("--noise-pct", type=float, default=DEFAULT_NOISE_PCT, help="aggregate timing noise band")
    compare_builds.add_argument(
        "--min-improvement-pct",
        type=float,
        default=DEFAULT_MIN_IMPROVEMENT_PCT,
        help="minimum median build improvement to accept",
    )
    compare_builds.add_argument("--json", help="write JSON comparison")
    compare_builds.add_argument("--markdown", help="write markdown comparison")
    compare_builds.set_defaults(func=command_compare_builds)

    record = subparsers.add_parser("record-experiment", help="store current diff and optional probe comparison")
    record.add_argument("--name", required=True, help="experiment name")
    record.add_argument("--path", action="append", default=[], help="candidate path to include in the diff")
    record.add_argument("--baseline", help="baseline probe JSON")
    record.add_argument("--candidate", help="candidate probe JSON")
    record.add_argument("--baseline-build", help="baseline build-timing JSON")
    record.add_argument("--candidate-build", help="candidate build-timing JSON")
    record.add_argument(
        "--allow-unrelated-dirty",
        action="store_true",
        help="allow dirty files outside --path when recording",
    )
    record.set_defaults(func=command_record_experiment)

    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except subprocess.TimeoutExpired as exc:
        print(f"command timed out: {exc}", file=sys.stderr)
        return 124
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

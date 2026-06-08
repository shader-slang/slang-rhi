#!/usr/bin/env python3

import argparse
import os
import pathlib
import re
import sys
from typing import List, Optional, Tuple


LEAK_HEADER_RE = re.compile(r"^(Direct|Indirect) leak of .+ allocated from:$")
FRAME_RE = re.compile(r"^\s*#(?P<index>\d+)\s+")
PROJECT_SOURCE_DIRS = ("src/", "include/", "tests/")
PROJECT_SYMBOL_RE = re.compile(r"\brhi::")


def normalize_path_text(value: str) -> str:
    return value.replace("\\", "/").lower()


def frame_index(line: str) -> Optional[int]:
    match = FRAME_RE.match(line)
    return int(match.group("index")) if match else None


def is_allocator_interceptor_frame(line: str) -> bool:
    index = frame_index(line)
    if index is None:
        return False

    lower = line.lower()
    allocator_markers = (
        "__interceptor_",
        "asan_malloc",
        "lsan_interceptors",
        "sanitizer_common",
        " in malloc ",
        " in calloc ",
        " in realloc ",
        " in operator new",
        " in operator new[]",
    )

    return any(marker in lower for marker in allocator_markers) or (
        index == 0 and "slang-rhi-tests" in lower
    )


def is_project_source_frame(line: str, repo_root: str) -> bool:
    normalized = normalize_path_text(line)
    repo = normalize_path_text(repo_root).rstrip("/") + "/"
    repo_index = normalized.find(repo)
    if repo_index < 0:
        return False

    relative = normalized[repo_index + len(repo) :]
    return relative.startswith(PROJECT_SOURCE_DIRS)


def is_project_binary_frame(line: str) -> bool:
    normalized = normalize_path_text(line)
    return "slang-rhi-tests+" in normalized or "slang-rhi-tests.exe+" in normalized


def has_slang_rhi_frame(block: List[str], repo_root: str) -> bool:
    for line in block:
        if frame_index(line) is None:
            continue
        if is_allocator_interceptor_frame(line):
            continue
        if is_project_source_frame(line, repo_root):
            return True
        if is_project_binary_frame(line):
            return True
        if PROJECT_SYMBOL_RE.search(line):
            return True
    return False


def extract_leak_blocks(text: str) -> List[List[str]]:
    lines = text.splitlines()
    blocks: List[List[str]] = []
    index = 0

    while index < len(lines):
        if not LEAK_HEADER_RE.match(lines[index]):
            index += 1
            continue

        block = [lines[index]]
        index += 1
        while index < len(lines):
            line = lines[index]
            if not line.strip():
                break
            if LEAK_HEADER_RE.match(line):
                index -= 1
                break
            if line.startswith("SUMMARY:") or line.startswith("----") or line.startswith("Suppressions used:"):
                break
            block.append(line)
            index += 1
        blocks.append(block)
        index += 1

    return blocks


def read_log(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail only for LeakSanitizer reports caused by slang-rhi frames.")
    parser.add_argument("--log-dir", type=pathlib.Path)
    parser.add_argument("--repo-root", type=pathlib.Path)
    args = parser.parse_args()

    if args.log_dir:
        log_dir = args.log_dir
    elif os.environ.get("SANITIZER_LOG_DIR"):
        log_dir = pathlib.Path(os.environ["SANITIZER_LOG_DIR"])
    else:
        print("No sanitizer log directory provided and SANITIZER_LOG_DIR is not set.")
        return 1
    repo_root_arg = args.repo_root or pathlib.Path(os.environ.get("GITHUB_WORKSPACE", os.getcwd()))
    repo_root = str(repo_root_arg.resolve())
    if not log_dir.exists():
        print(f"No sanitizer log directory found: {log_dir}")
        return 0

    leak_blocks: List[Tuple[pathlib.Path, List[str]]] = []
    project_blocks: List[Tuple[pathlib.Path, List[str]]] = []

    for log_path in sorted(path for path in log_dir.iterdir() if path.is_file()):
        text = read_log(log_path)
        if "LeakSanitizer" not in text:
            continue
        for block in extract_leak_blocks(text):
            leak_blocks.append((log_path, block))
            if has_slang_rhi_frame(block, repo_root):
                project_blocks.append((log_path, block))

    if project_blocks:
        print(
            f"::error::LeakSanitizer found {len(project_blocks)} leak block(s) with slang-rhi frames."
        )
        for log_path, block in project_blocks:
            print(f"\n{log_path}:")
            print("\n".join(block))
        return 1

    if leak_blocks:
        print(
            f"Ignored {len(leak_blocks)} LeakSanitizer leak block(s) with no slang-rhi frames after allocator frames."
        )
    else:
        print("No LeakSanitizer leak reports found.")

    return 0


if __name__ == "__main__":
    sys.exit(main())

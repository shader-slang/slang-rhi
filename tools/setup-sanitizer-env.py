#!/usr/bin/env python3

import argparse
import os
import pathlib
import shutil


def sanitizer_path(path: pathlib.Path) -> str:
    return str(path).replace("\\", "/")


def append_github_env(name: str, value: str) -> None:
    env_file = os.environ.get("GITHUB_ENV")
    if not env_file:
        print(f"{name}={value}")
        return
    with open(env_file, "a", encoding="utf-8") as file:
        file.write(f"{name}={value}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Configure sanitizer environment variables for CI.")
    parser.add_argument("--config", required=True)
    parser.add_argument("--os", required=True)
    args = parser.parse_args()

    workspace = pathlib.Path(os.environ.get("GITHUB_WORKSPACE", os.getcwd())).resolve()
    test_working_dir = workspace / "build" / args.config
    asan_suppressions = os.path.relpath(workspace / "tools" / "asan-suppressions.txt", test_working_dir)

    symbolizer = (
        shutil.which("llvm-symbolizer")
        or shutil.which("llvm-symbolizer-18")
        or shutil.which("llvm-symbolizer-17")
    )
    if symbolizer:
        append_github_env("ASAN_SYMBOLIZER_PATH", symbolizer)

    asan_options = [
        "halt_on_error=1",
        "symbolize=1",
        "fast_unwind_on_malloc=0",
        f"suppressions={sanitizer_path(pathlib.Path(asan_suppressions))}",
    ]

    if args.os == "linux":
        sanitizer_log_dir = test_working_dir / "sanitizer-logs"
        sanitizer_log_dir.mkdir(parents=True, exist_ok=True)
        lsan_suppressions = os.path.relpath(workspace / "tools" / "lsan-suppressions.txt", test_working_dir)

        asan_options = [
            "detect_leaks=1",
            "protect_shadow_gap=0",
            *asan_options,
        ]
        append_github_env(
            "LSAN_OPTIONS",
            f"suppressions={sanitizer_path(pathlib.Path(lsan_suppressions))}:exitcode=0:log_path=sanitizer-logs/lsan.log",
        )
        append_github_env("SANITIZER_LOG_DIR", str(sanitizer_log_dir))

    append_github_env("ASAN_OPTIONS", ":".join(asan_options))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

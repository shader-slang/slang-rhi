#!/usr/bin/env python3

import argparse
import os
import pathlib
import shutil
import subprocess
from typing import Optional


def sanitizer_path(path: pathlib.Path) -> str:
    return str(path).replace("\\", "/")


def append_github_env(name: str, value: str) -> None:
    env_file = os.environ.get("GITHUB_ENV")
    if not env_file:
        print(f"{name}={value}")
        return
    with open(env_file, "a", encoding="utf-8") as file:
        file.write(f"{name}={value}\n")


def find_symbolizer() -> Optional[str]:
    clang = shutil.which("clang++")
    if clang:
        result = subprocess.run(
            [clang, "--print-prog-name=llvm-symbolizer"],
            check=True,
            capture_output=True,
            text=True,
        )
        symbolizer = shutil.which(result.stdout.strip())
        if symbolizer:
            return symbolizer

    return (
        shutil.which("llvm-symbolizer")
        or shutil.which("llvm-symbolizer-18")
        or shutil.which("llvm-symbolizer-17")
    )


def deploy_windows_asan_runtime(test_working_dir: pathlib.Path) -> None:
    clang = shutil.which("clang++")
    if not clang:
        raise RuntimeError("Could not find clang++ while locating the Windows ASAN runtime.")

    runtime_name = "clang_rt.asan_dynamic-x86_64.dll"
    result = subprocess.run(
        [clang, f"--print-file-name={runtime_name}"],
        check=True,
        capture_output=True,
        text=True,
    )
    runtime_path = pathlib.Path(result.stdout.strip())
    if not runtime_path.is_file():
        raise RuntimeError(f"Could not locate the Windows ASAN runtime: {runtime_path}")

    destination = test_working_dir / runtime_name
    shutil.copy2(runtime_path, destination)
    print(f"Copied Windows ASAN runtime to {destination}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Configure sanitizer environment variables for CI.")
    parser.add_argument("--config", required=True)
    parser.add_argument("--os", required=True)
    parser.add_argument("--sanitizer", choices=("asan-ubsan", "tsan"), default="asan-ubsan")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()

    workspace = pathlib.Path(os.environ.get("GITHUB_WORKSPACE", os.getcwd())).resolve()
    test_working_dir = workspace / args.build_dir / args.config

    if args.sanitizer == "tsan":
        tsan_options = [
            "halt_on_error=1",
            "second_deadlock_stack=1",
        ]
        if args.os == "linux":
            # Vendor GPU runtimes are not TSAN-instrumented, so their internal
            # synchronization is invisible to the runtime and can produce false positives.
            tsan_options.append("ignore_noninstrumented_modules=1")
        append_github_env("TSAN_OPTIONS", ":".join(tsan_options))
        return 0

    asan_suppressions = os.path.relpath(workspace / "tools" / "asan-suppressions.txt", test_working_dir)

    symbolizer = find_symbolizer()
    if symbolizer:
        append_github_env("ASAN_SYMBOLIZER_PATH", symbolizer)

    asan_options = [
        "halt_on_error=1",
        "symbolize=1",
        "fast_unwind_on_malloc=0",
    ]

    # The Windows ASAN runtime rejects interceptor_via_lib suppressions. The
    # suppression file only contains Unix library names, so do not pass it there.
    if args.os != "windows":
        asan_options.append(f"suppressions={sanitizer_path(pathlib.Path(asan_suppressions))}")

    if args.os == "linux":
        sanitizer_log_dir = test_working_dir / "sanitizer-logs"
        sanitizer_log_dir.mkdir(parents=True, exist_ok=True)
        xdg_runtime_dir = test_working_dir / "xdg-runtime"
        xdg_runtime_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        xdg_runtime_dir.chmod(0o700)
        asan_options = [
            "detect_leaks=1",
            "protect_shadow_gap=0",
            # exitcode is a sanitizer-common flag even when supplied through
            # LSAN_OPTIONS. Keep leak-only reports non-fatal for the filter
            # below, but make ASAN/UBSAN failures terminate with SIGABRT.
            "abort_on_error=1",
            *asan_options,
        ]
        append_github_env(
            "LSAN_OPTIONS",
            "exitcode=0:log_path=sanitizer-logs/lsan.log",
        )
        append_github_env("SANITIZER_LOG_DIR", str(sanitizer_log_dir))
        append_github_env("XDG_RUNTIME_DIR", str(xdg_runtime_dir))

    if args.os == "windows":
        deploy_windows_asan_runtime(test_working_dir)

    append_github_env("ASAN_OPTIONS", ":".join(asan_options))
    append_github_env("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

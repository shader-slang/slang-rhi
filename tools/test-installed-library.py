"""Build and run a public API consumer against a relocated shared-library install."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--devices", nargs="*", default=[])
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    cache = {}
    for line in (build_dir / "CMakeCache.txt").read_text().splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    if cache.get("SLANG_RHI_BUILD_SHARED") != "ON":
        parser.error("the build must have SLANG_RHI_BUILD_SHARED=ON")

    source_dir = Path(__file__).resolve().parent.parent / "tests" / "public-api"
    # All staging, moves, and cleanup are confined to this fresh build subdirectory.
    with tempfile.TemporaryDirectory(prefix="installed-test-", dir=build_dir) as temporary:
        work_dir = Path(temporary)
        stage = work_dir / "stage"
        subprocess.run(
            ["cmake", "--install", str(build_dir), "--config", args.config, "--prefix", str(stage)],
            check=True,
        )
        prefix = work_dir / "relocated"
        stage.rename(prefix)
        consumer_source = work_dir / "consumer"
        shutil.copytree(source_dir, consumer_source)
        consumer_build = work_dir / "build"
        subprocess.run(
            [
                "cmake", "-S", str(consumer_source), "-B", str(consumer_build),
                "-G", "Ninja Multi-Config",
                f"-DCMAKE_CXX_COMPILER={cache['CMAKE_CXX_COMPILER']}",
                f"-DRHI_INSTALL_DIR={prefix}",
                f"-DRHI_INSTALL_LIBDIR={cache['CMAKE_INSTALL_LIBDIR']}",
                f"-DSLANG_INCLUDE_DIR={cache['SLANG_RHI_SLANG_INCLUDE_DIR']}",
                f"-DSLANG_LIBRARY_DIR={Path(cache['SLANG_RHI_SLANG_BINARY_DIR']) / 'lib'}",
            ],
            check=True,
        )
        subprocess.run(["cmake", "--build", str(consumer_build), "--config", args.config], check=True)
        executable = consumer_build / args.config / "slang-rhi-installed-test"
        env = os.environ.copy()
        if os.name == "nt":
            executable = executable.with_suffix(".exe")
            env["PATH"] = str(prefix / cache["CMAKE_INSTALL_BINDIR"]) + os.pathsep + env.get("PATH", "")
        # On Unix the loader must use the installed RPATH, with no build-tree
        # library search path supplied by this test.
        subprocess.run([str(executable), *args.devices], cwd=prefix, env=env, check=True)


if __name__ == "__main__":
    main()

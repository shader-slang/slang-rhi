# Reusing doctest-parallel

[doctest-parallel.h](../tests/doctest-parallel.h) is a single-header extension for
running doctest tests through a dynamic queue of persistent worker processes.
It requires C++17, doctest, the standard library, and native OS process/thread
support. It does not include or link Slang or slang-rhi. The doctest implementation
is unchanged.

## Minimal integration

Copy `doctest-parallel.h` into your project and make `doctest.h` available on the
include path. Define `DOCTEST_PARALLEL_IMPLEMENT` in exactly one translation unit.
That can also be the translation unit containing doctest's implementation:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT
#define DOCTEST_PARALLEL_IMPLEMENT
#include "doctest-parallel.h"

int main(int argc, char** argv)
{
    if (auto result = doctest::parallel::run(argc, argv))
        return *result;
    return doctest::Context(argc, argv).run();
}
```

Other test files can include `doctest.h` normally. Use a custom `main` instead of
`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`. With CMake, enable `cxx_std_17` and link
`Threads::Threads`; no extra runtime library is needed when the implementation
is compiled into the executable. This repository provides an interface target
named `doctest-parallel` with those requirements.

```sh
./my-tests --jobs=4
./my-tests --jobs=2 --test-timeout=60 -tc="buffer-*"
```

Without `--jobs`, or for doctest queries, `run()` returns `std::nullopt` so the
existing sequential entry point handles the invocation. Otherwise it returns the
overall exit code. Filters, subcases, skip decorators, and `--first`/`--last` ranges
remain under doctest's control. Tests are ordered by name for stable assignment.
The extension uses doctest's platform and registration macros, supports
`DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES`, and respects `DOCTEST_CONFIG_OPTIONS_PREFIX`
and `DOCTEST_CONFIG_NO_UNPREFIXED_OPTIONS` for doctest options. These configuration
macros must match across translation units. The extension's own options remain
`--jobs` and `--test-timeout`.

## Project callbacks

Pass a `doctest::parallel::Options` object to adapt an existing harness. All
callbacks are optional:

| Option | Runs in | Purpose |
|--------|---------|---------|
| `reporters` | Worker | Comma-separated doctest reporter names; defaults to `console`. |
| `workerStart(logPath)` | Worker | Initialize resources once, after output redirection. Nonzero fails startup. |
| `workerStop()` | Worker | Release resources before returning to `main`. Nonzero fails the run. |
| `configureContext(context)` | Coordinator and worker | Apply the same project defaults and filters during discovery and execution. |
| `isSkipped(test)` | Worker | Recognize a project's custom runtime skip mechanism. |
| `collectData()` | Worker | Return optional per-assignment metadata, including binary data, up to 64 KiB. |
| `onResult(result)` | Coordinator | Consume each assignment's result and metadata. Calls are serialized. |
| `coordinatorStart()` | Coordinator | Initialize aggregation after discovery, before launching workers. |
| `coordinatorStop()` | Coordinator | Validate aggregate project results. Nonzero fails the run. |
| `logDirectory()` | Coordinator | Supply an isolated, caller-owned directory for worker logs. |

`Result` contains the assigned test's name, exit code, number of executed and
runtime-skipped test cases, elapsed assignment time, a worker-failure flag, and
optional metadata. Metadata is only valid for completed assignments. Initialization
and cleanup failures affect the overall exit code separately from assignment
results. Callbacks must not throw. `configureContext` must produce matching test
selection during discovery and execution; scheduling and reporter options are
applied afterward by the runner.

Parse project-wide command-line options before calling `run()`. Put expensive test
resource initialization in `workerStart`, so the coordinator can discover tests
without creating devices or compiler sessions. Workers re-launch the same executable
with the same arguments and an internal worker option. They run tests sequentially
through fresh doctest contexts while retaining process-wide project caches.
Worker assignments validate test identity (name, suite, file, and line) against
discovery, and `run()` preserves an already-installed doctest context.

The slang-rhi-specific adapter is in [main.cpp](../tests/main.cpp).
It uses callbacks for devices, runtime skip messages, memory reports, and aggregate
GPU execution checks; these concepts do not appear in the reusable header.

## Process and output behavior

Each worker has its own control pipes. Standard output and error are redirected to
a worker log and replayed after each assignment. Default logs are created in a
unique system temporary directory and removed after the coordinator finishes.
A supplied `logDirectory` remains owned by the caller and is not removed.
Reporters run per assignment; aggregate XML/JUnit output is not provided.

Assertion failures leave a worker available for the next test. Crashes and timeouts
fail the assigned test, replace the worker, and continue the queue without retrying
the failed assignment. The optional timeout also covers startup and shutdown.
Final worker process exit codes are checked even after `workerStop` succeeds.

Tests must tolerate independent processes and varying execution order. Resources
outside the process, including shared files and GPU adapters, still require project
coordination. There is currently no exclusive-resource scheduling API. Windows
transport is tested here; the POSIX transport requires validation on Linux/macOS.

If the extension itself is built into a shared library, define
`DOCTEST_PARALLEL_CONFIG_IMPLEMENTATION_IN_DLL` consistently for its implementation
and consumers. It uses doctest's symbol export/import macros independently of
whether doctest itself is built as a DLL.

## Standalone verification

The fixture executable includes the extension from two translation units, disables
short doctest macros and unprefixed doctest options, and uses a custom option prefix.
It links only the doctest extension and native threads, without Slang or RHI code.
Its intentional failure, abrupt exit, and hanging tests are skipped by default and
selected explicitly by the Python integration checks.

```sh
cmake --build build --config Debug --target doctest-parallel-tests
python tools/test-doctest-parallel.py build/Debug/doctest-parallel-tests.exe
python tools/test-parallel-runner.py build/Debug/slang-rhi-tests.exe
```

The standalone checks cover subcases, filters and ranges, multiple translation
units, persistent worker lifecycle, binary metadata, custom skips, assertions,
worker startup/shutdown failures, abrupt exits, timeouts, and sequential fallback.

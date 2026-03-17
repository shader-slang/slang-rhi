# Task: Standardize Debug Layer Diagnostic Messages

## Overview

Reformat all diagnostic messages in the debug layer (`src/debug-layer/`) to follow
a consistent style. Also update the test file that checks for specific message substrings.

## How to find all messages

Search for these macro invocations across `src/debug-layer/`:

- `RHI_VALIDATION_ERROR(`
- `RHI_VALIDATION_WARNING(`
- `RHI_VALIDATION_INFO(`
- `RHI_VALIDATION_FORMAT(`
- `RHI_VALIDATION_ERROR_FORMAT(`

These are defined in `src/debug-layer/debug-helper-functions.h`. The macros auto-prepend
the current API function name to every message (e.g., `"IDevice::createBuffer: <message>"`).

## Message formatting rules

Apply ALL of the following rules to every message string:

### 1. Trailing period
All messages MUST end with a period.
```
"Buffer size must be greater than 0."    // correct
"Buffer size must be greater than 0"     // wrong
```

### 2. Sentence-case capitalization
Start with an uppercase letter, UNLESS the first word is a code identifier
(parameter name, field name, etc.).
```
"Buffer size must be greater than 0."    // correct — concept word
"outTexture must not be null."           // correct — code identifier starts lowercase
"too many color attachments."            // wrong — should be "Too many..."
```

### 3. No redundant function-name prefix
The macros already prepend the function name. Do NOT repeat it in the message.
```
"'pipeline' must not be null."                // correct
"bindPipeline: pipeline is null"              // wrong — "bindPipeline:" is redundant
```

### 4. Null-check style
Use the pattern: `"'paramName' must not be null."`
Single-quote the identifier. This replaces all other patterns:
```
"'outTexture' must not be null."              // correct
"outTexture is null"                          // wrong
"Texture view requires a non-null texture"    // wrong
"buffer must be valid"                        // wrong
"scratchBuffer must be provided"              // wrong
"instanceBuffer cannot be null."              // wrong
```

### 5. Spell out source/destination
Always use "source" and "destination", never "Src"/"Dest"/"src"/"dst".
Exception: struct field paths in format strings keep their C++ field name
(e.g., `"dstDescs[%d].rowCount"`).
```
"Source layer is out of bounds."              // correct
"Src layer is out of bounds."                 // wrong
"'dst' must not be null."                     // correct (parameter name)
"destination range out of bounds"             // wrong (no period, no caps)
```

### 6. Use "not supported" not "unsupported"
```
"Format is not supported."                    // correct
"Unsupported format"                          // wrong
```

### 7. Single quotes for identifiers, no backticks
When referencing code identifiers in prose, wrap them in single quotes.
Never use backticks (these messages appear in console output, not markdown).
```
"'index' must not exceed 'entryPointCount'."  // correct
"`index` must not exceed `entryPointCount`."  // wrong
```

### 8. Severity must match behavior
If the code path returns an error result (e.g., `SLANG_E_INVALID_ARG`),
the message MUST use `RHI_VALIDATION_ERROR`, not `RHI_VALIDATION_WARNING`.

## Known bugs to fix

These are pre-existing bugs to fix while reformatting:

1. **Typo** in `src/debug-layer/debug-surface.cpp`: `"aquired"` → `"acquired"`.

2. **Double-word typo** in `src/debug-layer/debug-helper-functions.cpp` (appears twice):
   `"must must not be zero for row-major and column-major layoutslayouts"`
   → `"must not be zero for row-major and column-major layouts."`
   This is caused by a broken string literal concatenation (missing space between
   adjacent string literals).

3. **Comma bug** in `src/debug-layer/debug-helper-functions.cpp` at the
   `AccelerationStructureBuildDesc::inputs[%d].type is not supported` message:
   Two string literals are separated by a comma instead of being adjacent,
   causing the second string to be passed as a printf argument (undefined behavior).
   Remove the comma so the strings concatenate.

4. **Severity mismatch** in `src/debug-layer/debug-helper-functions.cpp`:
   Two `RHI_VALIDATION_WARNING` calls in `validateAccelerationStructureBuildDesc`
   (`inputCount must be >= 1` and `inputs must have the same type`) return
   `SLANG_E_INVALID_ARG` — change these to `RHI_VALIDATION_ERROR`.

## Updating tests

The file `tests/test-debug-layer-validation.cpp` uses `capture.hasError("substring")`
and `capture.hasWarning("substring")` to check that specific validation messages are
produced. These use **substring matching**, so many checks will survive unchanged.

However, you MUST update test assertions where:

- The **substring itself changed** — e.g., null-check messages changed from
  `"outBuffer is null"` to `"'outBuffer' must not be null."`. The old substring
  `"outBuffer is null"` won't match. Update to `"'outBuffer' must not be null"`.

- The **severity changed** — the two WARNING→ERROR fixes mean any `hasWarning()`
  checks for those messages must become `hasError()`.

- The **wording changed** — e.g., `"Unsupported format"` → `"Format is not supported."`,
  `"Texture view requires a non-null texture"` → `"'texture' must not be null."`.

After making changes, grep the test file for every old substring you changed to make
sure none were missed.

## Files to modify

- `src/debug-layer/debug-device.cpp` — ~100 messages
- `src/debug-layer/debug-command-encoder.cpp` — ~125 messages
- `src/debug-layer/debug-helper-functions.cpp` — ~55 messages + bug fixes
- `src/debug-layer/debug-surface.cpp` — ~8 messages + typo fix
- `src/debug-layer/debug-shader-object.cpp` — ~3 messages
- `src/debug-layer/debug-heap.cpp` — ~4 messages
- `src/debug-layer/debug-command-queue.cpp` — ~1 message
- `src/debug-layer/debug-query.cpp` — ~1 message
- `src/debug-layer/debug-fence.cpp` — ~1 message
- `tests/test-debug-layer-validation.cpp` — update substring assertions

## Build and test

1. Build: `cmake --build ./build --config Debug`
2. If cmake errors occur, reconfigure: `cmake --preset msvc --fresh` (Windows)
   or `cmake --preset gcc --fresh` (Linux)
3. Run tests: `./build/Debug/slang-rhi-tests.exe`
4. Run formatting: `pre-commit run --all-files` (may modify files; re-run to confirm clean)

## Verification

After all changes, these greps should return zero matches in `src/debug-layer/`:

- Messages missing trailing period (excluding macro definitions):
  Search for `RHI_VALIDATION_ERROR("` or `RHI_VALIDATION_WARNING("` lines where the
  string does not end with `.")`
- Redundant function prefixes: `RHI_VALIDATION_ERROR(".*: ` pattern
- Old abbreviations: `"Src ` or `"Dest ` in message strings
- Typos: `aquired`, `must must`, `layoutslayouts`
- Backticks in messages: `` ` `` inside `RHI_VALIDATION_*` string arguments

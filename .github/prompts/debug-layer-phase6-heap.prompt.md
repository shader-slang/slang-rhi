## Phase 6: DebugHeap Validation — [debug-heap.cpp](src/debug-layer/debug-heap.cpp)

Add basic parameter validation to `DebugHeap` methods that are currently pure forwarding.

### Context

- `DebugHeap` wraps `IHeap`. All 5 methods currently just forward without any checks.
- This is low priority but straightforward to add.
- Use `RHI_VALIDATION_ERROR(...)` / `RHI_VALIDATION_WARNING(...)` consistent with the rest of the debug layer.
- **Documentation**: After implementing validation for each step, update the corresponding method's doc comment in [slang-rhi.h](include/slang-rhi.h) to document parameter constraints, valid usage rules, and error conditions. Use the validation checks as the source of truth for what to document.

### Steps

#### Step 1: `allocate`
Add:
- `outAllocation == nullptr` → error
- Validate allocation desc fields if applicable (check `HeapAllocDesc` for size/alignment constraints)

#### Step 2: `free`
Add:
- `allocation == nullptr` → `RHI_VALIDATION_WARNING("free: allocation is null")`

#### Step 3: `report` / `flush` / `removeEmptyPages`
These are management/diagnostic operations with no meaningful parameters to validate. Leave as pure forwarding.

### Verification

- Build: `cmake --build ./build --config Debug`
- Run tests: `./build/Debug/slang-rhi-tests.exe` — all existing tests must pass
- Ensure new validation only fires on genuinely invalid input, not on anything the test suite exercises
- Run `pre-commit run --all-files` to fix formatting


GitHub Copilot: # Technical Plan: Slang API Capture/Replay Wrapper

## Overview

A wrapper layer for the Slang compiler API (slang.h) to enable:
1. **Capture**: Record all API calls to a file
2. **Replay**: Re-execute captured calls for bug reproduction
3. **Sync Test**: Verify deterministic API usage across runs

The wrapper will live in **slang-rhi** and capture Slang API access only.

---

## API Surface Summary

| Category | Count | Priority |
|----------|-------|----------|
| COM Interfaces (methods) | ~120 | High |
| Reflection wrapper methods | ~150 | High (crash-prone) |
| Free functions | ~10 | High |
| Enums/Structs | ~50 | Medium (serialization) |

### Key Interfaces to Wrap

```
IGlobalSession     - Session creation, global config
ISession           - Module loading, type operations
IModule            - Entry points, serialization
IComponentType     - Linking, specialization, code gen
IEntryPoint        - Function reflection access
IBlob              - Data containers
ISlangFileSystem   - File I/O interception
```

### Reflection Types (Crash-Prone - High Priority)

```
TypeReflection / TypeLayoutReflection
VariableReflection / VariableLayoutReflection
FunctionReflection / GenericReflection
ShaderReflection / EntryPointReflection
DeclReflection / Attribute
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    slang-rhi / SGL                       │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│              slang-capture (in slang-rhi)                │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────┐ │
│  │ COM Proxies  │ │ Reflection   │ │ Capture Engine   │ │
│  │              │ │ Wrappers     │ │                  │ │
│  └──────┬───────┘ └──────┬───────┘ └────────┬─────────┘ │
│         │                │                  │           │
│         └────────────────┴──────────────────┘           │
│                          │                              │
│              ┌───────────┴───────────┐                  │
│              │   Serializer (JSON)   │                  │
│              └───────────────────────┘                  │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│                  Real Slang API (slang.h)                │
└─────────────────────────────────────────────────────────┘
```

---

## Serialization Format

### JSON Lines Format (Human Readable)

Each API call is a single JSON line for streaming and grep-ability:

```jsonl
{"seq":1,"ts":0.000,"tid":1,"iface":"IGlobalSession","method":"createSession","id":1,"args":{"desc":{"targets":[{"format":"SLANG_SPIRV"}],"searchPaths":["/shaders"]}}}
{"seq":2,"ts":0.001,"tid":1,"result":"SLANG_OK","out":{"session":2}}
{"seq":3,"ts":0.002,"tid":1,"iface":"ISession","method":"loadModule","id":2,"args":{"name":"MyModule"}}
{"seq":4,"ts":0.003,"tid":1,"result":"SLANG_OK","out":{"module":3}}
{"seq":5,"ts":0.004,"tid":1,"iface":"TypeReflection","method":"getKind","id":100,"args":{}}
{"seq":6,"ts":0.004,"tid":1,"result":"Struct"}
```

### Schema

```cpp
struct CaptureEntry {
    uint64_t seq;           // Sequence number
    double timestamp;       // Seconds since capture start
    uint32_t thread_id;     // Thread making the call

    std::string interface;  // "IGlobalSession", "TypeReflection", etc.
    std::string method;     // "createSession", "getKind", etc.
    uint64_t object_id;     // Object being called on

    json args;              // Method arguments

    // Set after call returns:
    std::string result;     // "SLANG_OK", "SLANG_FAIL", or return value
    json out_params;        // Output parameters, new object IDs
};
```

### Blob Handling

Large data (shader source, compiled code) stored separately with hash references:

```jsonl
{"seq":10,"iface":"ISession","method":"loadModuleFromSource","id":2,"args":{"name":"Test","source":"@blob:sha256:abc123"}}
```

Blobs stored in companion file or embedded base64 for small sizes:
```
capture.jsonl        # Call log
capture.blobs/       # Directory of blob files
  sha256_abc123.bin
  sha256_def456.bin
```

---

## Implementation Design

### Core Classes

```cpp
// slang-rhi/src/slang-capture/capture-engine.h

namespace slang_capture {

enum class CaptureMode {
    Disabled,       // No capture
    Capture,        // Record to file
    Replay,         // Playback from file
    SyncTest        // Compare against previous capture
};

class CaptureEngine {
public:
    static CaptureEngine& instance();

    void setMode(CaptureMode mode);
    void setOutputPath(const std::string& path);
    void setReferencePath(const std::string& path); // For sync test

    // Called by wrappers
    uint64_t beginCall(const char* iface, const char* method, uint64_t objectId);
    void addArg(uint64_t callId, const char* name, const json& value);
    void endCall(uint64_t callId, const char* result, const json& outParams = {});

    // Object ID management
    uint64_t registerObject(void* ptr, const char* type);
    void* getObject(uint64_t id);
    void releaseObject(uint64_t id);

    // Blob storage
    std::string storeBlob(const void* data, size_t size);
    std::vector<uint8_t> loadBlob(const std::string& ref);

    // Sync test
    bool verifySyncMatch(); // Returns false if mismatch detected

private:
    CaptureMode m_mode = CaptureMode::Disabled;
    std::ofstream m_output;
    std::mutex m_mutex;

    std::atomic<uint64_t> m_nextSeq{1};
    std::atomic<uint64_t> m_nextObjectId{1};

    std::unordered_map<void*, uint64_t> m_ptrToId;
    std::unordered_map<uint64_t, void*> m_idToPtr;

    // Sync test state
    std::ifstream m_reference;
    uint64_t m_referenceSeq = 0;
    bool m_syncMismatch = false;
};

} // namespace slang_capture
```

### COM Interface Proxy Template

```cpp
// slang-rhi/src/slang-capture/proxy-base.h

template<typename TInterface>
class CaptureProxy : public TInterface {
protected:
    Slang::ComPtr<TInterface> m_real;
    uint64_t m_objectId;

    CaptureProxy(TInterface* real)
        : m_real(real)
        , m_objectId(CaptureEngine::instance().registerObject(this, typeid(TInterface).name()))
    {}

    ~CaptureProxy() {
        CaptureEngine::instance().releaseObject(m_objectId);
    }

    // Helper for recording calls
    class CallRecorder {
        uint64_t m_callId;
    public:
        CallRecorder(const char* iface, const char* method, uint64_t objId)
            : m_callId(CaptureEngine::instance().beginCall(iface, method, objId)) {}

        template<typename T>
        CallRecorder& arg(const char* name, const T& value) {
            CaptureEngine::instance().addArg(m_callId, name, toJson(value));
            return *this;
        }

        void result(SlangResult r, const json& out = {}) {
            CaptureEngine::instance().endCall(m_callId, resultToString(r), out);
        }

        template<typename T>
        void result(const T& value) {
            CaptureEngine::instance().endCall(m_callId, toJson(value).dump());
        }
    };
};
```

### Example: IGlobalSession Proxy

```cpp
// slang-rhi/src/slang-capture/global-session-proxy.h

class GlobalSessionProxy : public CaptureProxy<slang::IGlobalSession> {
public:
    GlobalSessionProxy(slang::IGlobalSession* real) : CaptureProxy(real) {}

    SlangResult createSession(slang::SessionDesc const& desc, slang::ISession** outSession) override {
        CallRecorder rec("IGlobalSession", "createSession", m_objectId);
        rec.arg("desc", desc);

        slang::ISession* realSession = nullptr;
        SlangResult result = m_real->createSession(desc, &realSession);

        if (SLANG_SUCCEEDED(result) && realSession) {
            auto proxy = new SessionProxy(realSession);
            *outSession = proxy;
            rec.result(result, {{"session", proxy->objectId()}});
        } else {
            rec.result(result);
        }
        return result;
    }

    SlangProfileID findProfile(char const* name) override {
        CallRecorder rec("IGlobalSession", "findProfile", m_objectId);
        rec.arg("name", name);

        SlangProfileID result = m_real->findProfile(name);
        rec.result(static_cast<int>(result));
        return result;
    }

    // ... ~20 more methods
};
```

### Reflection Wrapper (Critical for Crash Debugging)

```cpp
// slang-rhi/src/slang-capture/reflection-capture.h

// Wrap the reflection types that are C++ wrappers over C API
class TypeReflectionCapture {
    slang::TypeReflection* m_real;
    uint64_t m_objectId;

public:
    TypeReflectionCapture(slang::TypeReflection* real)
        : m_real(real)
        , m_objectId(CaptureEngine::instance().registerObject(real, "TypeReflection"))
    {}

    slang::TypeReflection::Kind getKind() {
        CallRecorder rec("TypeReflection", "getKind", m_objectId);
        auto result = m_real->getKind();
        rec.result(kindToString(result));
        return result;
    }

    unsigned int getFieldCount() {
        CallRecorder rec("TypeReflection", "getFieldCount", m_objectId);
        auto result = m_real->getFieldCount();
        rec.result(result);
        return result;
    }

    VariableReflectionCapture getFieldByIndex(unsigned int index) {
        CallRecorder rec("TypeReflection", "getFieldByIndex", m_objectId);
        rec.arg("index", index);
        auto result = m_real->getFieldByIndex(index);
        // Note: may return nullptr - capture this!
        uint64_t resultId = result ?
            CaptureEngine::instance().registerObject(result, "VariableReflection") : 0;
        rec.result("OK", {{"field", resultId}});
        return VariableReflectionCapture(result);
    }

    char const* getName() {
        CallRecorder rec("TypeReflection", "getName", m_objectId);
        auto result = m_real->getName();
        rec.result(result ? result : "(null)");
        return result;
    }

    // ... other methods
};
```

---

## Sync Test Mode

### Purpose
Verify that a program makes identical Slang API calls across runs. This helps:
- Detect non-determinism in shader compilation
- Validate that changes don't affect API usage patterns
- Test the capture system itself

### Implementation

```cpp
// In CaptureEngine

void CaptureEngine::beginSyncTest(const std::string& referencePath) {
    m_mode = CaptureMode::SyncTest;
    m_reference.open(referencePath);
    m_syncMismatch = false;
}

void CaptureEngine::endCall(uint64_t callId, const char* result, const json& outParams) {
    // Build current call JSON
    json currentCall = buildCallJson(callId, result, outParams);

    if (m_mode == CaptureMode::SyncTest) {
        // Read expected call from reference
        std::string expectedLine;
        if (std::getline(m_reference, expectedLine)) {
            json expectedCall = json::parse(expectedLine);

            // Compare (ignoring timestamps and exact object IDs)
            if (!callsMatch(currentCall, expectedCall)) {
                m_syncMismatch = true;
                logMismatch(currentCall, expectedCall);
            }
        } else {
            m_syncMismatch = true;
            logError("More calls than expected");
        }
    }

    if (m_mode == CaptureMode::Capture) {
        m_output << currentCall.dump() << "\n";
        m_output.flush();
    }
}

bool CaptureEngine::callsMatch(const json& a, const json& b) {
    // Compare interface, method, and arguments
    // Object IDs may differ between runs, so compare by structure
    return a["iface"] == b["iface"] &&
           a["method"] == b["method"] &&
           argsMatch(a["args"], b["args"]);
}
```

### Usage

```bash
# First run: capture reference
SLANG_CAPTURE_MODE=capture SLANG_CAPTURE_FILE=reference.jsonl ./my_app

# Second run: verify sync
SLANG_CAPTURE_MODE=sync_test SLANG_CAPTURE_REF=reference.jsonl ./my_app
# Exit code 0 = match, 1 = mismatch
```

---

## Environment Variables

| Variable | Values | Description |
|----------|--------|-------------|
| `SLANG_CAPTURE_MODE` | `disabled`, `capture`, `replay`, `sync_test` | Operating mode |
| `SLANG_CAPTURE_FILE` | path | Output file for capture, input for replay |
| `SLANG_CAPTURE_REF` | path | Reference file for sync test |
| `SLANG_CAPTURE_BLOBS` | `inline`, external, `skip` | Blob storage mode |
| `SLANG_CAPTURE_REFLECTION` | `0`, `1` | Enable reflection call capture |

---

## File Structure in slang-rhi

```
slang-rhi/
├── src/
│   └── slang-capture/
│       ├── capture-engine.h
│       ├── capture-engine.cpp
│       ├── proxy-base.h
│       ├── json-serialization.h
│       ├── json-serialization.cpp
│       ├── proxies/
│       │   ├── global-session-proxy.h
│       │   ├── global-session-proxy.cpp
│       │   ├── session-proxy.h
│       │   ├── session-proxy.cpp
│       │   ├── module-proxy.h
│       │   ├── module-proxy.cpp
│       │   ├── component-type-proxy.h
│       │   ├── component-type-proxy.cpp
│       │   └── ...
│       ├── reflection/
│       │   ├── type-reflection-capture.h
│       │   ├── variable-reflection-capture.h
│       │   ├── function-reflection-capture.h
│       │   └── ...
│       ├── replay/
│       │   ├── replay-engine.h
│       │   └── replay-engine.cpp
│       └── sync-test/
│           ├── sync-verifier.h
│           └── sync-verifier.cpp
├── include/
│   └── slang-capture/
│       └── slang-capture.h    # Public API
└── tests/
    └── slang-capture/
        ├── test-capture-basic.cpp
        ├── test-sync-mode.cpp
        └── test-replay.cpp
```

---

## Incremental AI-Assisted Implementation Plan

This section describes how to implement the system iteratively using an AI coding assistant, with each step being independently testable.

### Phase 1: Core Infrastructure (Foundation)

#### Step 1.1: Create CaptureEngine Skeleton
**Task**: Create the basic `CaptureEngine` class with mode switching and file output.

**Files to create**:
- `slang-rhi/src/slang-capture/capture-engine.h`
- `slang-rhi/src/slang-capture/capture-engine.cpp`

**Test**:
```cpp
// Manual test in a scratch file
CaptureEngine::instance().setMode(CaptureMode::Capture);
CaptureEngine::instance().setOutputPath("test.jsonl");
auto id = CaptureEngine::instance().beginCall("Test", "testMethod", 1);
CaptureEngine::instance().endCall(id, "OK");
// Verify test.jsonl contains valid JSON line
```

**Acceptance**: File written, can be parsed as JSON.

#### Step 1.2: Add JSON Serialization Helpers
**Task**: Create `toJson()` functions for Slang types (`SessionDesc`, `TargetDesc`, enums).

**Files to create**:
- `slang-rhi/src/slang-capture/json-serialization.h`
- `slang-rhi/src/slang-capture/json-serialization.cpp`

**Test**:
```cpp
slang::SessionDesc desc;
desc.targetCount = 1;
json j = toJson(desc);
assert(j["targetCount"] == 1);
```

**Acceptance**: Round-trip serialization works for key types.

#### Step 1.3: Object ID Registry
**Task**: Implement object registration, lookup, and release.

**Test**:
```cpp
void* ptr = (void*)0x12345;
uint64_t id = CaptureEngine::instance().registerObject(ptr, "Test");
assert(CaptureEngine::instance().getObject(id) == ptr);
CaptureEngine::instance().releaseObject(id);
assert(CaptureEngine::instance().getObject(id) == nullptr);
```

**Acceptance**: IDs are unique, lookup works, release cleans up.

---

### Phase 2: First COM Proxy (Proof of Concept)

#### Step 2.1: Proxy Base Template
**Task**: Create `CaptureProxy<T>` base class and `CallRecorder` helper.

**Files to create**:
- `slang-rhi/src/slang-capture/proxy-base.h`

**Test**: Compile-only test that template instantiates.

#### Step 2.2: IGlobalSession Proxy (Minimal)
**Task**: Wrap just 3 methods: `createSession`, `findProfile`, `getBuildTagString`.

**Files to create**:
- `slang-rhi/src/slang-capture/proxies/global-session-proxy.h`
- `slang-rhi/src/slang-capture/proxies/global-session-proxy.cpp`

**Test**:
```cpp
// Enable capture
CaptureEngine::instance().setMode(CaptureMode::Capture);
CaptureEngine::instance().setOutputPath("global-session-test.jsonl");

// Create wrapped session
slang::IGlobalSession* realSession;
slang::createGlobalSession(SLANG_API_VERSION, &realSession);
auto* proxy = new GlobalSessionProxy(realSession);

// Use it
auto profile = proxy->findProfile("sm_6_0");
auto tag = proxy->getBuildTagString();

// Verify capture file has 2 calls
```

**Acceptance**: Capture file shows `findProfile` and `getBuildTagString` calls with correct args.

#### Step 2.3: Factory Function with Capture Toggle
**Task**: Create `slang_capture::createGlobalSession()` that returns proxy or real based on mode.

**Test**:
```cpp
// With capture enabled
setenv("SLANG_CAPTURE_MODE", "capture");
auto* session = slang_capture::createGlobalSession();
// Verify it's a proxy

// With capture disabled
setenv("SLANG_CAPTURE_MODE", "disabled");
auto* session2 = slang_capture::createGlobalSession();
// Verify it's the real thing
```

**Acceptance**: Mode switching works via environment variable.

---

### Phase 3: ISession Proxy (Module Loading)

#### Step 3.1: ISession Proxy
**Task**: Wrap `loadModule`, `loadModuleFromSource`, `createCompositeComponentType`.

**Test**:
```cpp
// Create session via proxy
auto* globalSession = slang_capture::createGlobalSession();
slang::ISession* session;
slang::SessionDesc desc = {...};
globalSession->createSession(desc, &session);

// Load a module
auto* module = session->loadModule("TestModule");

// Verify capture shows:
// 1. createSession call
// 2. loadModule call with "TestModule" arg
```

**Acceptance**: Module loading captured with module name.

#### Step 3.2: Blob Capture for Source Code
**Task**: Implement blob storage for `loadModuleFromSource`.

**Test**:
```cpp
const char* source = "float4 main() { return 0; }";
auto* module = session->loadModuleFromSource("Test", "test.slang", source);

// Verify capture file has blob reference
// Verify blob file exists with source content
```

**Acceptance**: Large source code stored externally, referenced by hash.

---

### Phase 4: Reflection Capture (Crash-Prone APIs)

#### Step 4.1: TypeReflection Wrapper
**Task**: Wrap `TypeReflection` methods that are known to crash.

**Key methods**:
- `getKind()`, `getName()`, `getFieldCount()`, `getFieldByIndex()`
- `getElementType()`, `getElementCount()`, `getRowCount()`, `getColumnCount()`

**Test**:
```cpp
// Get reflection from a loaded module
auto* module = session->loadModule("TestModule");
auto* reflection = module->getLayout();
auto* type = reflection->findTypeByName("MyStruct");

// Wrap it
TypeReflectionCapture wrapped(type);
auto kind = wrapped.getKind();
auto name = wrapped.getName();
auto fieldCount = wrapped.getFieldCount();

// Verify capture shows all 3 calls
```

**Acceptance**: Reflection calls captured with return values.

#### Step 4.2: Null Pointer Handling
**Task**: Ensure null returns from reflection are captured safely.

**Test**:
```cpp
// Ask for a type that doesn't exist
auto* type = reflection->findTypeByName("NonExistent");
TypeReflectionCapture wrapped(type); // type is null

// This should not crash, and should log the null
auto kind = wrapped.getKind(); // Should handle gracefully
```

**Acceptance**: Null pointers logged as `{"result": null}`, no crash.

#### Step 4.3: VariableReflection, FunctionReflection
**Task**: Add wrappers for other reflection types.

**Test**: Similar pattern to TypeReflection.

---

### Phase 5: Sync Test Mode

#### Step 5.1: Reference File Reader
**Task**: Implement reading and parsing reference capture file.

**Test**:
```cpp
// Create a reference file manually
// {"seq":1,"iface":"IGlobalSession","method":"findProfile","args":{"name":"sm_6_0"}}

CaptureEngine::instance().setMode(CaptureMode::SyncTest);
CaptureEngine::instance().setReferencePath("reference.jsonl");

// Make the same call
proxy->findProfile("sm_6_0");

// Should match
assert(CaptureEngine::instance().verifySyncMatch());
```

**Acceptance**: Matching calls pass, different calls fail.

#### Step 5.2: Mismatch Reporting
**Task**: Log detailed diff when sync test fails.

**Test**:
```cpp
// Reference expects "sm_6_0", we call with "sm_5_0"
proxy->findProfile("sm_5_0");

// Should fail and print diff
assert(!CaptureEngine::instance().verifySyncMatch());
// Check log shows: expected "sm_6_0", got "sm_5_0"
```

**Acceptance**: Clear error message showing expected vs actual.

#### Step 5.3: Sequence Number Verification
**Task**: Verify calls happen in same order.

**Test**:
```cpp
// Reference: findProfile then getBuildTagString
// Actual: getBuildTagString then findProfile
// Should detect order mismatch
```

**Acceptance**: Out-of-order calls detected.

---

### Phase 6: Complete Coverage

#### Step 6.1: IModule Proxy
**Methods**: `findEntryPointByName`, `getDefinedEntryPointCount`, `serialize`, `getName`, `getFilePath`

#### Step 6.2: IComponentType Proxy
**Methods**: `getLayout`, `getEntryPointCode`, `specialize`, `link`

#### Step 6.3: IEntryPoint Proxy
**Methods**: `getFunctionReflection`

#### Step 6.4: Remaining Reflection Types
- `TypeLayoutReflection`
- `VariableLayoutReflection`
- `EntryPointReflection`
- `ShaderReflection`
- `GenericReflection`
- `DeclReflection`

---

### Phase 7: Integration and Testing

#### Step 7.1: Integration with slang-rhi
**Task**: Replace direct Slang API usage with capture wrappers.

**Test**: Run existing slang-rhi tests with capture enabled, verify capture files generated.

#### Step 7.2: Integration Test Suite
**Task**: Create tests that:
1. Run a shader compilation with capture
2. Run again with sync test against capture
3. Verify determinism

#### Step 7.3: Replay Engine (Optional)
**Task**: Implement replay from capture file.

---

## Testing Strategy for AI Assistant

Each step should include:

1. **Clear file locations**: Exact paths for new/modified files
2. **Minimal scope**: One concept per step
3. **Concrete test**: Copy-pasteable test code
4. **Acceptance criteria**: How to verify success
5. **Dependencies**: Which previous steps must be complete

### AI Prompting Template

```
Implement Step X.Y: [Step Name]

Context:
- Previous steps completed: [list]
- Relevant existing files: [list]

Task: [Description]

Files to create/modify:
- [path]: [description]

Test code:
```cpp
[test code]
```

Acceptance criteria:
- [criterion 1]
- [criterion 2]

Notes:
- [any special considerations]
```

### Iterative Verification

After each step:
1. Build the project
2. Run the specific test
3. Fix any issues
4. Commit with message referencing step number
5. Proceed to next step

---

## Estimated Effort by Phase

| Phase | Description | Effort |
|-------|-------------|--------|
| 1 | Core Infrastructure | 2-3 days |
| 2 | First COM Proxy | 2 days |
| 3 | ISession Proxy | 2 days |
| 4 | Reflection Capture | 3-4 days |
| 5 | Sync Test Mode | 2 days |
| 6 | Complete Coverage | 1 week |
| 7 | Integration | 2-3 days |
| **Total** | | **3-4 weeks** |

---

## Summary

This design provides:

1. **Human-readable JSON** capture format for easy debugging
2. **Reflection call capture** to debug crash-prone APIs
3. **Sync test mode** for determinism verification
4. **Incremental implementation** with clear test points
5. **AI-friendly structure** with small, testable steps

The capture system will live entirely in slang-rhi, requiring no changes to the Slang API itself, and can be enabled/disabled via environment variables for zero overhead when not needed.

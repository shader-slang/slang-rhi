# Error handling and diagnostics

Slang RHI reports errors through two related mechanisms:

- API calls return `rhi::Result`, which is a typedef of `SlangResult`.
- Diagnostic text is delivered out of band through `IDebugCallback` and, for some shader APIs, `ISlangBlob` outputs.

For APIs that return `Result`, treat it as the authoritative success or failure signal. Treat diagnostics as explanatory context that helps a user or developer understand why the result was returned.

## Client API perspective

Most public interfaces in `include/slang-rhi.h` that can fail synchronously return `rhi::Result`. Always check returned results with `SLANG_SUCCEEDED(result)` or `SLANG_FAILED(result)`.

Common result values include, but are not limited to:

- `SLANG_OK`: the operation succeeded.
- `SLANG_FAIL`: a generic failure, often from a backend or native API call.
- `SLANG_E_INVALID_ARG`: an invalid pointer, descriptor, range, enum, or other client argument.
- `SLANG_E_INVALID_HANDLE`: an invalid native or platform handle.
- `SLANG_E_NOT_AVAILABLE`: the requested feature or operation is not available on this backend, device, or platform.
- `SLANG_E_NOT_IMPLEMENTED`: the API exists but the backend has no implementation.
- `SLANG_E_OUT_OF_MEMORY`: an allocation failed.
- `SLANG_E_BUFFER_TOO_SMALL`: the provided output buffer is too small.
- `SLANG_E_NOT_FOUND`: the requested item was not found.
- `SLANG_E_TIME_OUT`: a wait operation timed out.

Some command-recording interfaces, such as draw, dispatch, copy, barrier, and debug-marker methods, return `void`. When validation is enabled, invalid use of those methods is reported through the debug callback and the debug layer may skip the invalid command. For these APIs, the callback message can be the only immediate failure signal.

Not every failure result carries a detailed machine-readable reason. For detailed text, install a debug callback before creating the device.

```cpp
class MyDebugCallback : public rhi::IDebugCallback
{
public:
    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        rhi::DebugMessageType type,
        rhi::DebugMessageSource source,
        const char* message
    ) override
    {
        // Copy or log the message here.
        // The callback object is owned by the application.
    }
};

MyDebugCallback callback;

rhi::DeviceDesc desc = {};
desc.deviceType = rhi::DeviceType::D3D12;
desc.debugCallback = &callback;
desc.enableValidation = true;

Slang::ComPtr<rhi::IDevice> device;
rhi::Result result = rhi::getRHI()->createDevice(desc, device.writeRef());
if (SLANG_FAILED(result))
{
    // Use any messages collected by the callback to explain the failure.
}
```

`DeviceDesc::debugCallback` is a raw pointer. Slang RHI does not take ownership of it, so the application must keep the callback alive for at least as long as any device that uses it. If no callback is provided, devices use an internal no-op callback, so most diagnostics are silently ignored.

Callbacks may be invoked from the thread making an RHI call or from a backend/driver callback. Keep callback implementations thread-safe, avoid heavy work inside them, and copy the message text if it must be retained.

## Message types and sources

Each callback message has a severity:

- `DebugMessageType::Info`: informational status.
- `DebugMessageType::Warning`: suspicious behavior or optional functionality that could not be enabled.
- `DebugMessageType::Error`: invalid API usage, failed compilation, native API failure, device loss, or another operation failure.

Each callback message also has a source:

- `DebugMessageSource::Layer`: Slang RHI validation or infrastructure. Examples include null output pointers, invalid descriptors, unsupported validation options, or "debug layer is enabled" status.
- `DebugMessageSource::Driver`: backend, native graphics API, or downstream API debug layer output. Examples include failed D3D/Vulkan/CUDA/OptiX calls, Vulkan validation messages, D3D12 debug messages, WGPU uncaptured errors, and Metal compiler or runtime errors.
- `DebugMessageSource::Slang`: Slang compiler diagnostics from shader linking, specialization, or code generation.

Message text is intended for humans. It is not a stable parse format.

## Validation and downstream debug layers

Slang RHI has several separate validation and diagnostic controls.

`DeviceDesc::enableValidation` enables the Slang RHI validation layer for that device. The validation layer wraps the concrete backend device and checks RHI-level usage such as null pointers, invalid ranges, incompatible resource usage, invalid enum values, missing pipeline state, and command encoding mistakes. Validation errors from `Result`-returning APIs usually return `SLANG_E_INVALID_ARG` or `SLANG_FAIL` and emit `DebugMessageSource::Layer` messages. Validation errors from `void` command-recording APIs emit `DebugMessageSource::Layer` messages and may cause the invalid command to be skipped.

`IRHI::setDebugLayerOptions()` controls downstream API debug layers for supported native APIs. `coreValidation` applies to D3D11, D3D12, and Vulkan. `GPUAssistedValidation` applies to D3D12 and Vulkan.

```cpp
rhi::DebugLayerOptions options = {};
options.coreValidation = true;
options.GPUAssistedValidation = true;
options.required = false;
rhi::Result result = rhi::getRHI()->setDebugLayerOptions(options);
if (SLANG_FAILED(result))
{
    // Devices are already alive, or the options could not be changed.
}
```

These options are global to the RHI instance and must be changed before any devices are alive. If `required` is true, device creation fails when requested downstream debug layers cannot be enabled. If `required` is false, Slang RHI reports a warning and attempts to continue without the unavailable layer.

The legacy `IRHI::enableDebugLayers()` only requests core validation and is less precise than `setDebugLayerOptions()`.

`DeviceDesc::enableRayTracingValidation` requests downstream ray tracing validation for supported backends, currently CUDA/OptiX, D3D12 through NVAPI, and Vulkan through `VK_NV_ray_tracing_validation`. This is a per-device option and is separate from both the Slang RHI validation layer and `DebugLayerOptions`. If the requested validation mode is unavailable, backends report a warning or error through the callback and may continue without it.

`DeviceDesc::enableAftermath` enables NVIDIA Aftermath diagnostics for supported D3D11, D3D12, and Vulkan devices when Slang RHI is built with Aftermath support. `DeviceDesc::aftermathFlags` controls markers, resource tracking, call-stack capture, shader debug information, and shader error reporting. Aftermath is a crash-diagnostic facility, not a general validation layer, and some backends place additional constraints on when it can be combined with downstream debug layers.

## Shader diagnostics

Shader-related failures can report diagnostics through both callbacks and explicit blobs.

When an API has an `ISlangBlob** outDiagnosticBlob` parameter, pass a blob pointer and inspect it after failure:

```cpp
Slang::ComPtr<rhi::IShaderProgram> program;
Slang::ComPtr<ISlangBlob> diagnostics;
rhi::Result result = device->createShaderProgram(desc, program.writeRef(), diagnostics.writeRef());
if (diagnostics)
{
    const char* text = static_cast<const char*>(diagnostics->getBufferPointer());
    // Log text.
}
```

The callback path is still useful because shader compilation can also happen later, for example when creating pipelines or specializing shader programs. In those cases Slang diagnostics are emitted as `DebugMessageSource::Slang` messages.

`DeviceDesc::enableCompilationReports` is a performance reporting feature, not an error reporting feature. When enabled, `IShaderProgram::getCompilationReport()` and `IDevice::getCompilationReportList()` expose timing and cache-hit data for shader compilation and pipeline creation.

## Native backend diagnostics

Native-call diagnostics are implemented through `src/core/diagnostics.h` and `src/core/diagnostics.cpp`.

The shared helper records the failing call expression, native result value, native result name, optional detail text, and source location. The formatted message has this shape:

```text
<call> call failed
  result: <numeric-value> (<native-result-name>)
  detail: <optional-detail>
  location: <source-file>:<line>
```

`reportNativeCallError()` sends this text as `DebugMessageType::Error` and `DebugMessageSource::Driver` when a `Device*` is available. If no device is available, it prints to `stderr`.

The backend-specific wrappers add native result names and details:

- D3D11/D3D12 use `reportD3DError()`, `getHRESULTName()`, and `SLANG_D3D_RETURN_ON_FAIL_REPORT()`.
- Vulkan uses `reportVulkanError()`, `getVkResultName()`, `SLANG_VK_RETURN_ON_FAIL_REPORT()`, and `SLANG_VK_CHECK_REPORT()`. `VK_ERROR_DEVICE_LOST` also gives Aftermath a chance to write a crash dump when Aftermath is enabled.
- CUDA uses `reportCUDAError()` and `SLANG_CUDA_RETURN_ON_FAIL_REPORT()`, including `cuGetErrorName()` and `cuGetErrorString()` detail text.
- OptiX uses `reportOptixError()` and `SLANG_OPTIX_RETURN_ON_FAIL_REPORT()`, including OptiX error names and strings.
- WGPU reports device-lost, uncaptured-error, and callback-specific errors directly through the device callback.
- Metal reports Objective-C and Metal API errors directly through the device callback.

The `_REPORT` macros should be used whenever a diagnostic callback can be reached from the current object. They route diagnostics to the application's callback. Plain `RETURN_ON_FAIL` variants deliberately do not report; use them for backend probing, pre-device setup, or paths where another layer will emit the diagnostic.

## Internal guidelines

When adding or changing backend code:

- Return a specific `Result` when one is meaningful. Prefer `SLANG_E_INVALID_ARG` for invalid client input, `SLANG_E_NOT_AVAILABLE` for unavailable features, and `SLANG_FAIL` for native API failures that do not map cleanly to a public result.
- Validate inexpensive RHI-level preconditions before making native API calls, especially null output pointers, descriptor ranges, resource usage flags, and unsupported enum values.
- Use `Device::handleMessage()` or `Device::printInfo()`, `printWarning()`, and `printError()` for Slang RHI messages.
- Use the backend native-call macros for D3D, Vulkan, CUDA, and OptiX calls so the call expression, native result name, and source location are captured.
- Pass the most specific supported object available to reporting macros. CUDA reporting helpers accept both `Device*` and `DeviceChild*` through `getDiagnosticDevice()`; D3D, Vulkan, and OptiX reporting helpers currently expect a `Device*`.
- Avoid emitting duplicate diagnostics for the same failure. A single clear callback message plus the returned `Result` is usually enough.
- Do not depend on diagnostics for control flow. The returned `Result` must fully represent success or failure.
- Use `SLANG_RETURN_ON_FAIL()` to preserve and propagate failures from helper calls.
- Use `SLANG_RHI_DISABLE_NATIVE_CALL_ERROR_SCOPE()` only around intentional native probes where failure is expected and would otherwise create noisy diagnostics.

Assertions remain a separate mechanism for internal invariants. They are useful for impossible states during development, but public API error handling should return `Result` values and, when helpful, emit diagnostics through the callback.

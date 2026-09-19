"""Install identical diagnostic hooks into a disposable source export (not the working tree).

Usage: python tools/profile-bindings-wgpu.py build/profile-base-src
Build that export normally, then run benchmark-bindings.py against instrumented
executables. Logs contain binding-profile rows with per-sample phase, metric,
calls, inclusive nanoseconds, and amount (bytes or bind-group entries).
Timings include instrumentation overhead; use normal builds for performance claims.
"""

import argparse
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    working = Path(__file__).resolve().parents[1]
    if source == working or (source / ".git").exists():
        parser.error("use a disposable source export without .git, not a checkout")
    header = "binding-benchmark-profile.h"
    if (source / "src" / header).exists():
        parser.error("this export is already instrumented")
    edits = {}

    def read(name):
        if name not in edits:
            edits[name] = (source / name).read_text()
        return edits[name]

    def replace(name, old, new):
        text = read(name)
        if text.count(old) != 1:
            raise RuntimeError(f"Expected one matching instrumentation anchor in {name}: {old}")
        edits[name] = text.replace(old, new)

    def scope(name, signature, metric):
        text = read(name)
        start = text.index(signature)
        brace = text.index("{", start)
        edits[name] = text[:brace + 1] + f"\n    binding_profile::Scope profileScope(binding_profile::Metric::{metric});" + text[brace + 1:]

    api = "src/wgpu/wgpu-api.cpp"
    wrappers = '''
namespace {
template<binding_profile::Metric M, typename Fn> struct ProfileHook;
template<binding_profile::Metric M, typename R, typename... A>
struct ProfileHook<M, R (*)(A...)>
{
    static inline R (*original)(A...);
    static R call(A... args)
    {
        binding_profile::Scope scope(M, amount(args...));
        return original(args...);
    }
    static uint64_t amount(A... args)
    {
        auto tuple = std::make_tuple(args...);
        if constexpr (M == binding_profile::Metric::DeviceCreateBuffer)
            return std::get<1>(tuple)->size;
        else if constexpr (M == binding_profile::Metric::DeviceCreateBindGroup)
            return std::get<1>(tuple)->entryCount;
        else if constexpr (M == binding_profile::Metric::CommandEncoderCopyBufferToBuffer)
            return std::get<5>(tuple);
        else return 0;
    }
};
}
'''
    replace(api, '#include <filesystem>', f'#include <filesystem>\n#include <tuple>\n#include "../{header}"\n' + wrappers)
    names = ["DeviceCreateBindGroup", "DeviceCreateBuffer", "BufferMapAsync", "InstanceWaitAny",
             "BufferUnmap", "CommandEncoderCopyBufferToBuffer", "CommandEncoderFinish",
             "ComputePassEncoderSetBindGroup", "RenderPassEncoderSetBindGroup",
             "ComputePassEncoderDispatchWorkgroups", "RenderPassEncoderDraw", "QueueSubmit",
             "BindGroupAddRef", "BindGroupRelease", "BufferRelease"]
    install = "\n".join(
        f"    ProfileHook<binding_profile::Metric::{n}, WGPUProc{n}>::original = wgpu{n};\n"
        f"    wgpu{n} = &ProfileHook<binding_profile::Metric::{n}, WGPUProc{n}>::call;" for n in names)
    replace(api, "#undef LOAD_PROC", "#undef LOAD_PROC\n" + install)
    edits[api] = '#include "wgpu-shader-object.h"\n' + read(api)
    replace(api, "Result API::init()\n{", "Result API::init()\n{\n    std::printf(\"binding-profile-sizes,%zu,%zu,%zu\\n\", sizeof(ShaderObject), sizeof(RootShaderObject), sizeof(BindingDataBuilder));")
    command = "src/wgpu/wgpu-command.cpp"
    scope(command, "Result CommandEncoderImpl::getBindingData(", "BindingData")
    track = "storage.trackResources(rootObject);" if "storage.trackResources(rootObject);" in read(command) else "rootObject->trackResources(m_commandBuffer->m_trackedObjects);"
    replace(command, track, "{ binding_profile::Scope scope(binding_profile::Metric::TrackResources); " + track + " }")
    shader = "src/wgpu/wgpu-shader-object.cpp"
    scope(shader, "Result BindingDataBuilder::bindOrdinaryDataBufferIfNeeded(", "OrdinaryData")
    pool = "src/wgpu/wgpu-constant-buffer-pool.cpp"
    scope(pool, "Result ConstantBufferPool::allocate(", "AllocateUniform")
    scope(pool, "Result ConstantBufferPool::mapPage(", "MapPage")
    for name in (command, shader, pool):
        edits[name] = f'#include "../{header}"\n' + read(name)
    fixture = "tests/test-benchmark-bindings.cpp"
    edits[fixture] = f'#include "../src/{header}"\n' + read(fixture)
    replace(fixture, "auto start = Clock::now();", "binding_profile::begin();\n            auto start = Clock::now();")
    for anchor, phase in [("initialized", "Encode"), ("encoded", "Finish"), ("finished", "Submit"),
                          ("submitted", "Wait"), ("completed", "Retire")]:
        replace(fixture, f"auto {anchor} = Clock::now();", f"auto {anchor} = Clock::now();\n            binding_profile::phase(binding_profile::Phase::{phase});")
    replace(fixture, "auto retired = Clock::now();", "auto retired = Clock::now();\n            binding_profile::dump(workloadName, kModes[mode], count, sample);")
    # Validate every anchor before mutating the disposable export.
    shutil.copyfile(Path(__file__).with_name("benchmark-bindings-wgpu-profile.h"), source / "src" / header)
    for name, contents in edits.items():
        (source / name).write_text(contents)
    print(f"Installed diagnostic hooks in {source}")


if __name__ == "__main__":
    main()

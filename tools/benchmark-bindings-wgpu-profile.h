// Diagnostic-only instrumentation, installed into disposable source trees by
// profile-bindings-wgpu.py. Never included by the normal library or benchmarks.
#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace binding_profile {
enum class Phase
{
    Off,
    Setup,
    Encode,
    Finish,
    Submit,
    Wait,
    Retire,
    Count
};
enum class Metric
{
    BindingData,
    TrackResources,
    OrdinaryData,
    AllocateUniform,
    MapPage,
    DeviceCreateBindGroup,
    DeviceCreateBuffer,
    BufferMapAsync,
    InstanceWaitAny,
    BufferUnmap,
    CommandEncoderCopyBufferToBuffer,
    CommandEncoderFinish,
    ComputePassEncoderSetBindGroup,
    RenderPassEncoderSetBindGroup,
    ComputePassEncoderDispatchWorkgroups,
    RenderPassEncoderDraw,
    QueueSubmit,
    BindGroupAddRef,
    BindGroupRelease,
    BufferRelease,
    Count
};
inline constexpr const char* phaseNames[] = {"off", "setup", "encode", "finish", "submit", "wait", "retire"};
inline constexpr const char* metricNames[] = {
    "BindingData",
    "TrackResources",
    "OrdinaryData",
    "AllocateUniform",
    "MapPage",
    "DeviceCreateBindGroup",
    "DeviceCreateBuffer",
    "BufferMapAsync",
    "InstanceWaitAny",
    "BufferUnmap",
    "CommandEncoderCopyBufferToBuffer",
    "CommandEncoderFinish",
    "ComputePassEncoderSetBindGroup",
    "RenderPassEncoderSetBindGroup",
    "ComputePassEncoderDispatchWorkgroups",
    "RenderPassEncoderDraw",
    "QueueSubmit",
    "BindGroupAddRef",
    "BindGroupRelease",
    "BufferRelease"
};
struct Counter
{
    uint64_t calls = 0, ns = 0, amount = 0;
};
struct State
{
    Phase phase = Phase::Off;
    std::array<std::array<Counter, size_t(Metric::Count)>, size_t(Phase::Count)> counters{};
};
inline thread_local State state;
using Clock = std::chrono::steady_clock;
struct Scope
{
    Counter* counter;
    Clock::time_point start;
    explicit Scope(Metric metric, uint64_t amount = 0)
        : counter(state.phase == Phase::Off ? nullptr : &state.counters[size_t(state.phase)][size_t(metric)])
    {
        if (counter)
        {
            ++counter->calls;
            counter->amount += amount;
            start = Clock::now();
        }
    }
    ~Scope()
    {
        if (counter)
            counter->ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    }
};
inline void begin()
{
    state = {};
    state.phase = Phase::Setup;
}
inline void phase(Phase value)
{
    state.phase = value;
}
inline void dump(const char* workload, const char* mode, uint32_t count, uint32_t sample)
{
    state.phase = Phase::Off;
    if (!sample)
        return;
    for (size_t p = 1; p < size_t(Phase::Count); ++p)
        for (size_t m = 0; m < size_t(Metric::Count); ++m)
        {
            const auto& c = state.counters[p][m];
            if (c.calls)
                std::printf(
                    "binding-profile,%s,%s,%u,%u,%s,%s,%llu,%llu,%llu\n",
                    workload,
                    mode,
                    count,
                    sample,
                    phaseNames[p],
                    metricNames[m],
                    (unsigned long long)c.calls,
                    (unsigned long long)c.ns,
                    (unsigned long long)c.amount
                );
        }
}
} // namespace binding_profile

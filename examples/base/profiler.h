#pragma once

#include "utils.h"

#include <slang-rhi.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace rhi {

// Optional timings for explicitly marked work on one queue. Polling never waits;
// a full ring skips measurement rather than recycling queries still in flight.
class Profiler
{
public:
    static constexpr uint32_t kFrameCount = 4;
    static constexpr uint32_t kMaxRegions = 32;

    Profiler() = default;
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;
    Profiler(Profiler&&) = delete;
    Profiler& operator=(Profiler&&) = delete;

    // A logical frame may record and submit any number of command buffers on one
    // queue and recording thread. Submit or discard every instrumented buffer
    // before end() or destruction; never submit/replay it after the frame closes.
    // Successful end() publishes the measurement once all its queries resolve;
    // missing or invalid timestamps discard the entire measurement. An
    // early return discards the measurement, but submitted queries remain owned
    // until completion. Neither path waits for GPU work or submits commands.
    // Only one profiler frame may record on a thread at a time.
    class Frame
    {
    public:
        explicit Frame(Profiler& profiler)
            : m_profiler(profiler)
        {
        }
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;
        ~Frame()
        {
            if (m_begun)
            {
                ASSERT(s_current == &m_profiler, "Profiler frame must end on its recording thread");
                s_current = nullptr;
                m_profiler.endFrame(false);
            }
        }

        Result begin()
        {
            if (m_begun || s_current)
                return SLANG_E_INVALID_ARG;
            SLANG_RETURN_ON_FAIL(m_profiler.beginFrame());
            s_current = &m_profiler;
            m_begun = true;
            return SLANG_OK;
        }

        // Close the frame after its last submission. Query collection is deferred
        // until a later begin(); ending the frame does not imply GPU completion.
        Result end()
        {
            if (!m_begun || s_current != &m_profiler)
                return SLANG_E_INVALID_ARG;
            if (!m_profiler.m_regionStack.empty())
                return SLANG_E_INVALID_ARG;
            s_current = nullptr;
            m_profiler.endFrame(true);
            m_begun = false;
            return SLANG_OK;
        }

    private:
        Profiler& m_profiler;
        bool m_begun = false;
    };

    // Create a frame guard. Call begin() to start recording and handle failures.
    Frame frame() { return Frame(*this); }

    Result init(IDevice* device)
    {
        m_frequency = device->getInfo().timestampFrequency;
        if (!device->hasFeature(Feature::TimestampQuery) || !m_frequency)
            return SLANG_OK;
        for (auto& frame : m_frames)
        {
            QueryPoolDesc desc = {};
            desc.count = kMaxRegions * 2;
            desc.label = "Example profiler";
            Result result = device->createQueryPool(desc, frame.pool.writeRef());
            if (result == SLANG_E_NOT_AVAILABLE || result == SLANG_E_NOT_IMPLEMENTED)
                return SLANG_OK;
            SLANG_RETURN_ON_FAIL(result);
        }
        m_supported = true;
        return SLANG_OK;
    }

    bool enabled() const { return m_enabled; }
    // Call between frames. Pending queries remain owned until completion, but
    // their results must not reappear after profiling is toggled back on.
    void setEnabled(bool enabled)
    {
        m_enabled = enabled;
        m_lastSerial = m_serial;
        m_timingTotals.clear();
        m_displayTimings.clear();
        m_timingCount = 0;
        m_timingLastRefresh = 0;
    }

    // Format the averaged timings. Frame::begin updates them even when the overlay is hidden.
    std::string getDisplayText() const
    {
        if (!m_enabled)
            return "\n\nProfiler off\n";
        if (!m_supported)
            return "\n\nProfiler unavailable\n";
        if (m_displayTimings.empty())
            return "\n\nProfiler timing pending\n";

        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << "\n\n" << std::fixed << std::setprecision(2);
        // Walk children in first-seen order, keeping repeated regions under their parent.
        auto printChildren = [&](auto&& self, uint32_t parent, uint32_t depth) -> void
        {
            for (uint32_t i = 0; i < m_displayTimings.size(); ++i)
            {
                const auto& sample = m_displayTimings[i];
                if (sample.parent != parent)
                    continue;
                text << std::string(depth * 2, ' ') << sample.name << ": " << sample.milliseconds << " ms\n";
                self(self, i, depth + 1);
            }
        };
        printChildren(printChildren, kMaxRegions, 0);
        return text.str();
    }

    // Instrumentation uses the thread-local active frame. No active frame,
    // disabled/unsupported profiling, or a full query ring makes it a no-op.
    // Balance every push with a pop in the same frame, including skipped regions.
    // Accept either a pass encoder or a command encoder (e.g. for AS builds).
    template<typename EncoderOrPass>
    static void pushRegion(EncoderOrPass* encoderOrPass, const char* name)
    {
        auto* profiler = s_current;
        if (!profiler || profiler->m_active < 0)
            return;
        auto& frame = profiler->m_frames[profiler->m_active];
        uint32_t index = uint32_t(frame.regions.size());
        uint32_t parent = profiler->m_regionStack.empty() ? kMaxRegions : profiler->m_regionStack.back();
        // Keep skipped regions on the stack so their pops cannot close a measured parent.
        profiler->m_regionStack.push_back(index);
        if (index == kMaxRegions)
            return;
        frame.regions.push_back({name, parent});
        encoderOrPass->writeTimestamp(frame.pool, index * 2);
    }

    // Close the innermost region. Both endpoints must be on the same queue and
    // recording thread, but may be in different passes or submissions. Spanning
    // submissions measures elapsed GPU time, including any idle gap between them.
    template<typename EncoderOrPass>
    static void popRegion(EncoderOrPass* encoderOrPass)
    {
        auto* profiler = s_current;
        if (!profiler || profiler->m_active < 0)
            return;
        ASSERT(!profiler->m_regionStack.empty(), "popRegion requires a matching pushRegion");
        uint32_t index = profiler->m_regionStack.back();
        profiler->m_regionStack.pop_back();
        if (index == kMaxRegions)
            return;
        auto& frame = profiler->m_frames[profiler->m_active];
        encoderOrPass->writeTimestamp(frame.pool, index * 2 + 1);
    }

private:
    inline static thread_local Profiler* s_current = nullptr;

    struct Sample
    {
        std::string name;
        uint32_t parent;
        double milliseconds;
    };

    struct Region
    {
        std::string name;
        uint32_t parent;
    };

    struct QueryFrame
    {
        ComPtr<IQueryPool> pool;
        std::vector<Region> regions;
        uint64_t serial = 0;
        bool pending = false;
        bool publish = false;
    };

    // A closed frame is reusable once every submitted query has completed.
    bool isFrameReady(QueryFrame& frame)
    {
        uint32_t queryCount = uint32_t(frame.regions.size()) * 2;
        QueryResultState state;
        Result result = frame.pool->getResultState(0, queryCount, &state);
        if (SLANG_SUCCEEDED(result) && state != QueryResultState::Reset)
            return state == QueryResultState::Resolved;

        // Missing timestamps or state errors discard the entire measurement.
        // Reset on a range can hide pending queries beside unsubmitted ones,
        // so inspect each endpoint before releasing the slot. Unknown completion
        // also retains the slot until readiness can be established.
        frame.publish = false;
        for (uint32_t i = 0; i < queryCount; ++i)
            if (SLANG_FAILED(frame.pool->getResultState(i, 1, &state)) || state == QueryResultState::Pending)
                return false;
        return true;
    }

    std::vector<Sample> collectSamples(const QueryFrame& frame)
    {
        std::array<uint64_t, kMaxRegions * 2> ticks;
        uint32_t queryCount = uint32_t(frame.regions.size()) * 2;
        if (SLANG_FAILED(frame.pool->getResult(0, queryCount, ticks.data())))
            return {};

        std::vector<Sample> samples;
        // Map recorded regions to aggregated samples, including repeated parents.
        std::array<uint32_t, kMaxRegions> sampleIndices;
        for (size_t i = 0; i < frame.regions.size(); ++i)
        {
            // Discard invalid timestamps instead of displaying unsigned underflow.
            if (ticks[i * 2 + 1] < ticks[i * 2])
                return {};
            const auto& region = frame.regions[i];
            uint32_t parent = region.parent == kMaxRegions ? kMaxRegions : sampleIndices[region.parent];
            double milliseconds = double(ticks[i * 2 + 1] - ticks[i * 2]) * 1000.0 / m_frequency;
            auto sample = std::find_if(
                samples.begin(),
                samples.end(),
                [&](const Sample& s)
                {
                    return s.parent == parent && s.name == region.name;
                }
            );
            sampleIndices[i] = uint32_t(sample - samples.begin());
            if (sample == samples.end())
                samples.push_back({region.name, parent, milliseconds});
            else
                sample->milliseconds += milliseconds;
        }
        return samples;
    }

    void poll()
    {
        if (!m_supported)
            return;
        std::vector<Sample> latestSamples;
        for (auto& frame : m_frames)
        {
            if (!frame.pending || !isFrameReady(frame))
                continue;
            frame.pending = false;
            if (!frame.publish || frame.serial <= m_lastSerial)
                continue;
            auto samples = collectSamples(frame);
            if (!samples.empty())
            {
                latestSamples = std::move(samples);
                m_lastSerial = frame.serial;
            }
        }
        updateTimings(latestSamples);
    }

    // Average the latest completed frame from each poll, refreshing every 0.25 s.
    // Retain the last good display while newer measurements are pending or invalid.
    void updateTimings(const std::vector<Sample>& samples)
    {
        if (!samples.empty())
        {
            bool sameRegions = samples.size() == m_timingTotals.size();
            for (size_t i = 0; sameRegions && i < samples.size(); ++i)
                sameRegions =
                    samples[i].name == m_timingTotals[i].name && samples[i].parent == m_timingTotals[i].parent;
            if (!sameRegions)
            {
                // Do not mix different workloads (e.g. paused simulation or bloom off).
                m_timingTotals = samples;
                m_timingCount = 1;
                m_displayTimings.clear();
            }
            else
            {
                for (size_t i = 0; i < samples.size(); ++i)
                    m_timingTotals[i].milliseconds += samples[i].milliseconds;
                ++m_timingCount;
            }
        }
        double now = glfwGetTime();
        if (m_timingCount && (m_displayTimings.empty() || now - m_timingLastRefresh >= 0.25))
        {
            m_displayTimings = m_timingTotals;
            for (auto& sample : m_displayTimings)
                sample.milliseconds /= m_timingCount;
            for (auto& sample : m_timingTotals)
                sample.milliseconds = 0;
            m_timingCount = 0;
            m_timingLastRefresh = now;
        }
    }

    Result beginFrame()
    {
        poll();
        if (m_supported && m_enabled)
        {
            for (uint32_t i = 0; i < kFrameCount; ++i)
            {
                auto& frame = m_frames[i];
                if (frame.pending)
                    continue;
                SLANG_RETURN_ON_FAIL(frame.pool->reset());
                frame.regions.clear();
                frame.serial = ++m_serial;
                m_active = int(i);
                break;
            }
        }
        return SLANG_OK;
    }

    void endFrame(bool publish)
    {
        if (m_active >= 0)
        {
            auto& frame = m_frames[m_active];
            frame.pending = !frame.regions.empty();
            frame.publish = publish;
        }
        m_regionStack.clear();
        m_active = -1;
    }

    std::array<QueryFrame, kFrameCount> m_frames;
    std::vector<uint32_t> m_regionStack;
    uint64_t m_frequency = 0, m_serial = 0, m_lastSerial = 0;
    int m_active = -1;
    bool m_supported = false, m_enabled = true;
    std::vector<Sample> m_timingTotals, m_displayTimings;
    uint32_t m_timingCount = 0;
    double m_timingLastRefresh = 0;
};

} // namespace rhi

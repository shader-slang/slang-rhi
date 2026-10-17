#pragma once

#include "utils.h"

#include <slang-rhi.h>

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

    // Tokens belong to the originating profiler and recording. The profiler
    // must outlive its tokens; a default token represents no measurement.
    class RegionToken
    {
    public:
        RegionToken() = default;
        explicit operator bool() const { return m_profiler != nullptr; }

    private:
        RegionToken(Profiler* profiler, uint64_t serial, uint32_t index)
            : m_profiler(profiler)
            , m_serial(serial)
            , m_index(index)
        {
        }
        Profiler* m_profiler = nullptr;
        uint64_t m_serial = 0;
        uint32_t m_index = 0;
        friend class Profiler;
    };

    // End the scope before ending its pass. In particular, do not keep a scope
    // alive across passes; use beginRegion/endRegion for those intervals.
    class ScopedRegion
    {
    public:
        ScopedRegion(IPassEncoder* pass, const char* name)
            : m_pass(pass)
            , m_region(beginRegion(pass, name))
        {
        }
        ScopedRegion(const ScopedRegion&) = delete;
        ScopedRegion& operator=(const ScopedRegion&) = delete;
        ~ScopedRegion() { endRegion(m_pass, m_region); }

    private:
        IPassEncoder* m_pass;
        RegionToken m_region;
    };

    struct Sample
    {
        std::string name;
        double milliseconds;
    };

    // Keep this guard alive until submission. An early return discards the
    // measurement; the caller must also discard the unsubmitted command buffer.
    // Successful begin activates this profiler on the current thread until
    // end or destruction. End recording before submitting through the RHI,
    // then call markSubmitted only after the queue accepts the command buffers.
    // Recording scopes for different profilers may nest, but must end in stack
    // order on that same thread.
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
                if (!m_ended)
                {
                    ASSERT(s_current == &m_profiler, "Profiler frames must end in stack order");
                    s_current = m_previous;
                }
                m_profiler.cancelFrame();
            }
        }

        Result begin()
        {
            if (m_begun)
                return SLANG_E_INVALID_ARG;
            SLANG_RETURN_ON_FAIL(m_profiler.beginFrame());
            m_previous = s_current;
            s_current = &m_profiler;
            m_begun = true;
            m_ended = false;
            return SLANG_OK;
        }

        // Validate and close recording before submitting commands.
        // Keep the query slot reserved until markSubmitted() or destruction.
        Result end()
        {
            if (!m_begun || m_ended || s_current != &m_profiler)
                return SLANG_E_INVALID_ARG;
            SLANG_RETURN_ON_FAIL(m_profiler.validateFrame());
            s_current = m_previous;
            m_ended = true;
            return SLANG_OK;
        }

        // Notify after a successful queue submission. This cannot fail; early
        // returns before this notification cancel the unsubmitted measurement.
        void markSubmitted()
        {
            ASSERT(m_begun && m_ended, "Profiler frame must begin and end before submission notification");
            m_profiler.commitFrame();
            m_begun = false;
        }

    private:
        Profiler& m_profiler;
        Profiler* m_previous = nullptr;
        bool m_begun = false;
        bool m_ended = false;
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

    bool supported() const { return m_supported; }
    bool enabled() const { return m_enabled; }
    // Call between frames. Pending queries remain owned until completion, but
    // their results must not reappear after profiling is toggled back on.
    void setEnabled(bool enabled)
    {
        m_enabled = enabled;
        m_discardThrough = m_serial;
        m_samples.clear();
        m_timingTotals.clear();
        m_displayTimings.clear();
        m_timingCount = 0;
        m_timingSerial = 0;
        m_timingLastRefresh = 0;
    }
    uint64_t droppedFrames() const { return m_droppedFrames; }
    uint64_t sampleSerial() const { return m_lastSerial; }
    const std::vector<Sample>& samples() const { return m_samples; }

    // Format the latest observed samples, averaging and refreshing every 0.25 s.
    // Pass monotonically increasing time in seconds. This does not poll queries;
    // beginFrame() or poll() supplies new samples independently of the display.
    std::string getDisplayText(double timeSeconds)
    {
        if (!m_enabled)
            return "\n\nProfiler off\n";
        if (!m_supported)
            return "\n\nProfiler unavailable\n";
        if (m_samples.empty())
            return "\n\nProfiler timing pending\n";

        if (m_timingSerial != m_lastSerial)
        {
            m_timingSerial = m_lastSerial;
            bool sameRegions = m_samples.size() == m_timingTotals.size();
            for (size_t i = 0; sameRegions && i < m_samples.size(); ++i)
                sameRegions = m_samples[i].name == m_timingTotals[i].name;
            if (!sameRegions)
            {
                // Do not mix different workloads (e.g. paused simulation or bloom off).
                m_timingTotals = m_samples;
                m_timingCount = 1;
                m_displayTimings.clear();
            }
            else
            {
                for (size_t i = 0; i < m_samples.size(); ++i)
                    m_timingTotals[i].milliseconds += m_samples[i].milliseconds;
                ++m_timingCount;
            }
        }
        if (m_timingCount && (m_displayTimings.empty() || timeSeconds - m_timingLastRefresh >= 0.25))
        {
            m_displayTimings = m_timingTotals;
            for (auto& sample : m_displayTimings)
                sample.milliseconds /= m_timingCount;
            for (auto& sample : m_timingTotals)
                sample.milliseconds = 0;
            m_timingCount = 0;
            m_timingLastRefresh = timeSeconds;
        }

        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << "\n\n" << std::fixed << std::setprecision(2);
        for (const auto& sample : m_displayTimings)
            text << sample.name << ": " << sample.milliseconds << " ms\n";
        return text.str();
    }

    Result poll()
    {
        if (!m_supported)
            return SLANG_OK;
        for (auto& frame : m_frames)
        {
            if (!frame.pending)
                continue;
            QueryResultState state;
            SLANG_RETURN_ON_FAIL(frame.pool->getResultState(0, uint32_t(frame.names.size()) * 2, &state));
            if (state != QueryResultState::Resolved)
                continue;
            std::array<uint64_t, kMaxRegions * 2> ticks;
            SLANG_RETURN_ON_FAIL(frame.pool->getResult(0, uint32_t(frame.names.size()) * 2, ticks.data()));
            if (frame.serial > m_lastSerial && frame.serial > m_discardThrough)
            {
                m_samples.clear();
                for (size_t i = 0; i < frame.names.size(); ++i)
                {
                    // Omit invalid pairs instead of displaying unsigned underflow.
                    if (ticks[i * 2 + 1] < ticks[i * 2])
                        continue;
                    double milliseconds = double(ticks[i * 2 + 1] - ticks[i * 2]) * 1000.0 / m_frequency;
                    auto sample = std::find_if(
                        m_samples.begin(),
                        m_samples.end(),
                        [&](const Sample& s)
                        {
                            return s.name == frame.names[i];
                        }
                    );
                    if (sample == m_samples.end())
                        m_samples.push_back({frame.names[i], milliseconds});
                    else
                        sample->milliseconds += milliseconds;
                }
                m_lastSerial = frame.serial;
            }
            frame.pending = false;
        }
        return SLANG_OK;
    }

    // Instrumentation uses the thread-local active frame. No active frame,
    // disabled/unsupported profiling, or a full query ring makes it a no-op.
    static RegionToken beginRegion(IPassEncoder* pass, const char* name)
    {
        auto* profiler = s_current;
        if (!profiler || profiler->m_active < 0)
            return {};
        auto& frame = profiler->m_frames[profiler->m_active];
        if (frame.names.size() == kMaxRegions)
            return {};
        uint32_t index = uint32_t(frame.names.size());
        frame.names.emplace_back(name);
        pass->writeTimestamp(frame.pool, index * 2);
        return RegionToken(profiler, frame.serial, index);
    }

    // Both endpoints must be on the same queue; they may be in different passes.
    // Resolve through the token's owner, even if a different profiler is active.
    // Stale and already-closed tokens are ignored. Call on the recording thread.
    static void endRegion(IPassEncoder* pass, RegionToken region)
    {
        auto* profiler = region.m_profiler;
        if (!profiler || profiler->m_active < 0)
            return;
        auto& frame = profiler->m_frames[profiler->m_active];
        uint32_t index = region.m_index;
        if (region.m_serial == frame.serial && index < frame.names.size() && !frame.closed[index])
        {
            pass->writeTimestamp(frame.pool, index * 2 + 1);
            frame.closed[index] = true;
        }
    }

private:
    inline static thread_local Profiler* s_current = nullptr;

    Result beginFrame()
    {
        if (m_recording)
            return SLANG_E_INVALID_ARG;
        SLANG_RETURN_ON_FAIL(poll());
        if (m_supported && m_enabled)
        {
            for (uint32_t i = 0; i < kFrameCount; ++i)
            {
                auto& frame = m_frames[i];
                if (frame.pending)
                    continue;
                SLANG_RETURN_ON_FAIL(frame.pool->reset());
                frame.names.clear();
                frame.closed.fill(false);
                frame.serial = ++m_serial;
                m_active = int(i);
                break;
            }
            if (m_active < 0)
                ++m_droppedFrames;
        }
        m_recording = true;
        return SLANG_OK;
    }

    Result validateFrame() const
    {
        if (m_active >= 0)
        {
            const auto& frame = m_frames[m_active];
            for (size_t i = 0; i < frame.names.size(); ++i)
                if (!frame.closed[i])
                    return SLANG_E_INVALID_ARG;
        }
        return SLANG_OK;
    }

    void commitFrame()
    {
        if (m_active >= 0)
        {
            auto& frame = m_frames[m_active];
            frame.pending = !frame.names.empty();
        }
        cancelFrame();
    }

    void cancelFrame()
    {
        m_active = -1;
        m_recording = false;
    }

    struct QueryFrame
    {
        ComPtr<IQueryPool> pool;
        std::vector<std::string> names;
        std::array<bool, kMaxRegions> closed = {};
        uint64_t serial = 0;
        bool pending = false;
    };
    std::array<QueryFrame, kFrameCount> m_frames;
    std::vector<Sample> m_samples;
    uint64_t m_frequency = 0, m_serial = 0, m_lastSerial = 0, m_discardThrough = 0, m_droppedFrames = 0;
    int m_active = -1;
    bool m_supported = false, m_enabled = true, m_recording = false;
    std::vector<Sample> m_timingTotals, m_displayTimings;
    uint64_t m_timingSerial = 0;
    uint32_t m_timingCount = 0;
    double m_timingLastRefresh = 0;
};

} // namespace rhi

#define RHI_PROFILE_CONCAT_IMPL(a, b) a##b
#define RHI_PROFILE_CONCAT(a, b) RHI_PROFILE_CONCAT_IMPL(a, b)
#define PROFILE_SCOPE(pass, name)                                                                                      \
    ::rhi::Profiler::ScopedRegion RHI_PROFILE_CONCAT(profileScope_, __COUNTER__)(pass, name)

#pragma once

#include <slang-rhi.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace rhi {

// Optional timings for explicitly marked work on one queue. Polling never waits;
// a full ring skips measurement rather than recycling queries still in flight.
class GpuProfiler
{
public:
    static constexpr uint32_t kFrameCount = 4;
    static constexpr uint32_t kMaxRegions = 32;
    static constexpr uint32_t kInvalidRegion = ~0u;

    struct Sample
    {
        std::string name;
        double milliseconds;
    };

    // Keep this guard alive until submission. An early return discards the
    // measurement; the caller must also discard the unsubmitted command buffer.
    class Frame
    {
    public:
        explicit Frame(GpuProfiler& profiler)
            : m_profiler(profiler)
        {
        }
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;
        ~Frame()
        {
            if (m_begun)
                m_profiler.cancelFrame();
        }

        Result begin()
        {
            if (m_begun)
                return SLANG_E_INVALID_ARG;
            SLANG_RETURN_ON_FAIL(m_profiler.beginFrame());
            m_begun = true;
            return SLANG_OK;
        }

        Result submit(ICommandQueue* queue, const SubmitDesc& desc)
        {
            if (!m_begun || !queue)
                return SLANG_E_INVALID_ARG;
            SLANG_RETURN_ON_FAIL(m_profiler.validateFrame());
            SLANG_RETURN_ON_FAIL(queue->submit(desc));
            m_profiler.commitFrame();
            m_begun = false;
            return SLANG_OK;
        }

        Result submit(ICommandQueue* queue, ICommandBuffer* commands)
        {
            if (!commands)
                return SLANG_E_INVALID_ARG;
            SubmitDesc desc = {};
            desc.commandBuffers = &commands;
            desc.commandBufferCount = 1;
            return submit(queue, desc);
        }

    private:
        GpuProfiler& m_profiler;
        bool m_begun = false;
    };

    Result init(IDevice* device)
    {
        m_frequency = device->getInfo().timestampFrequency;
        if (!device->hasFeature(Feature::TimestampQuery) || !m_frequency)
            return SLANG_OK;
        for (auto& frame : m_frames)
        {
            QueryPoolDesc desc = {};
            desc.count = kMaxRegions * 2;
            desc.label = "Example GPU profiler";
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
    }
    uint64_t droppedFrames() const { return m_droppedFrames; }
    uint64_t sampleSerial() const { return m_lastSerial; }
    const std::vector<Sample>& samples() const { return m_samples; }

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

    uint32_t beginRegion(IPassEncoder* pass, const char* name)
    {
        if (m_active < 0)
            return kInvalidRegion;
        auto& frame = m_frames[m_active];
        if (frame.names.size() == kMaxRegions)
            return kInvalidRegion;
        uint32_t index = uint32_t(frame.names.size());
        frame.names.emplace_back(name);
        pass->writeTimestamp(frame.pool, index * 2);
        return index;
    }

    // Both endpoints must be on the same queue; they may be in different passes.
    void endRegion(IPassEncoder* pass, uint32_t region)
    {
        if (m_active < 0 || region == kInvalidRegion)
            return;
        auto& frame = m_frames[m_active];
        if (region < frame.names.size() && !frame.closed[region])
        {
            pass->writeTimestamp(frame.pool, region * 2 + 1);
            frame.closed[region] = true;
        }
    }

private:
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
};

} // namespace rhi

#pragma once

#include <slang-rhi.h>

#include <array>
#include <string>
#include <vector>

namespace rhi {

// Optional, nonblocking GPU timings for one queue. Regions use pass timestamps,
// which also work on backends that cannot timestamp the command encoder itself.
// beginFrame() polls old frames; endFrame() seals the recording BEFORE submission.
// Submit each recording once. Discarded recordings must call cancelFrame() instead.
// A full ring drops a sample, never waits or resets queries still in flight.
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
            SLANG_RETURN_ON_FAIL(device->createQueryPool(desc, frame.pool.writeRef()));
        }
        m_supported = true;
        return SLANG_OK;
    }

    bool supported() const { return m_supported; }
    uint64_t droppedFrames() const { return m_droppedFrames; }
    const std::vector<Sample>& samples() const { return m_samples; }

    Result poll()
    {
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
            if (frame.serial > m_lastSerial)
            {
                m_samples.clear();
                for (size_t i = 0; i < frame.names.size(); ++i)
                {
                    // Reject a disjoint/invalid pair rather than displaying an unsigned underflow.
                    if (ticks[i * 2 + 1] >= ticks[i * 2])
                        m_samples.push_back(
                            {frame.names[i], double(ticks[i * 2 + 1] - ticks[i * 2]) * 1000.0 / m_frequency}
                        );
                }
                m_lastSerial = frame.serial;
            }
            frame.pending = false;
        }
        return SLANG_OK;
    }

    Result beginFrame()
    {
        if (m_active >= 0)
            return SLANG_E_INVALID_ARG;
        SLANG_RETURN_ON_FAIL(poll());
        if (!m_supported)
            return SLANG_OK;
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
            return SLANG_OK;
        }
        ++m_droppedFrames;
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

    // A region may span several passes on the same queue (e.g. a simulation step).
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

    Result endFrame()
    {
        if (m_active < 0)
            return SLANG_OK;
        auto& frame = m_frames[m_active];
        for (size_t i = 0; i < frame.names.size(); ++i)
            if (!frame.closed[i])
                return SLANG_E_INVALID_ARG;
        frame.pending = !frame.names.empty();
        m_active = -1;
        return SLANG_OK;
    }

    void cancelFrame() { m_active = -1; }

private:
    struct Frame
    {
        ComPtr<IQueryPool> pool;
        std::vector<std::string> names;
        std::array<bool, kMaxRegions> closed = {};
        uint64_t serial = 0;
        bool pending = false;
    };
    std::array<Frame, kFrameCount> m_frames;
    std::vector<Sample> m_samples;
    uint64_t m_frequency = 0, m_serial = 0, m_lastSerial = 0, m_droppedFrames = 0;
    int m_active = -1;
    bool m_supported = false;
};

} // namespace rhi

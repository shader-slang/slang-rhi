#pragma once

#include "testing.h"
#include "stress/stress-op-log.h"
#include "stress/stress-rng.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace rhi::testing::stress {

struct StressStats
{
    uint64_t iterations = 0;
    uint64_t submits = 0;
    uint64_t waits = 0;
    uint64_t validations = 0;
    uint64_t buffersCreated = 0;
    uint64_t texturesCreated = 0;
    uint64_t viewsCreated = 0;
    uint64_t samplersCreated = 0;
    uint64_t accelerationStructuresCreated = 0;
    uint64_t shaderProgramsCreated = 0;
    uint64_t pipelinesCreated = 0;
};

class ResourceBudget
{
public:
    void reset(uint64_t budgetBytes)
    {
        m_budgetBytes = budgetBytes;
        m_currentBytes = 0;
        m_peakBytes = 0;
    }

    bool tryReserve(uint64_t bytes)
    {
        if (m_budgetBytes != 0 && bytes > m_budgetBytes - std::min(m_budgetBytes, m_currentBytes))
            return false;
        m_currentBytes += bytes;
        if (m_currentBytes > m_peakBytes)
            m_peakBytes = m_currentBytes;
        return true;
    }

    void release(uint64_t bytes)
    {
        m_currentBytes = bytes >= m_currentBytes ? 0 : m_currentBytes - bytes;
    }

    uint64_t budgetBytes() const { return m_budgetBytes; }
    uint64_t currentBytes() const { return m_currentBytes; }
    uint64_t peakBytes() const { return m_peakBytes; }

private:
    uint64_t m_budgetBytes = 0;
    uint64_t m_currentBytes = 0;
    uint64_t m_peakBytes = 0;
};

class StressContext
{
public:
    StressContext(GpuTestContext* ctx, IDevice* device, const char* scenarioName);

    bool shouldContinue() const;
    uint64_t beginIteration();
    bool shouldValidate() const;

    void submit(ComPtr<ICommandBuffer> commandBuffer);
    void wait();
    void finalWait();
    void flushReleasedResources();

    void recordOperation(std::string operation);
    void reportProgressIfDue();
    void reportFinal();
    void captureState() const;

    bool reserveBudget(uint64_t bytes) { return m_budget.tryReserve(bytes); }
    void releaseBudget(uint64_t bytes) { m_budget.release(bytes); }

    ICommandQueue* queue() const { return m_queue; }
    IDevice* device() const { return m_device; }
    const Options::StressOptions& options() const { return m_options; }
    StressStats& stats() { return m_stats; }
    const StressStats& stats() const { return m_stats; }
    Rng& rng() { return m_rng; }
    uint64_t derivedSeed() const { return m_derivedSeed; }
    uint64_t initialResourceCount() const { return m_initialResourceCount; }
    uint64_t currentResourceCount() const;

private:
    double elapsedSeconds() const;
    std::string summaryString() const;

    GpuTestContext* m_ctx = nullptr;
    IDevice* m_device = nullptr;
    ComPtr<ICommandQueue> m_queue;
    Options::StressOptions m_options;
    std::string m_scenarioName;
    uint64_t m_derivedSeed = 0;
    Rng m_rng;
    OperationLog m_log;
    StressStats m_stats;
    ResourceBudget m_budget;
    uint32_t m_submittedSinceWait = 0;
    uint64_t m_initialResourceCount = 0;
    std::chrono::steady_clock::time_point m_start;
    std::chrono::steady_clock::time_point m_lastReport;
};

} // namespace rhi::testing::stress

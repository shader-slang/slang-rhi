#include "stress/stress-context.h"

#include "device.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace rhi::testing::stress {

StressContext::StressContext(GpuTestContext* ctx, IDevice* device, const char* scenarioName)
    : m_ctx(ctx)
    , m_device(device)
    , m_options(rhi::testing::options().stress)
    , m_scenarioName(scenarioName)
    , m_log(m_options.logOps)
{
    REQUIRE(device != nullptr);
    m_queue = device->getQueue(QueueType::Graphics);
    REQUIRE(m_queue != nullptr);

    uint64_t adapterPart = 0xffffffffull;
    if (ctx && ctx->deviceType != DeviceType::Default)
    {
        int adapterIndex = rhi::testing::options().deviceAdapterIndex[size_t(ctx->deviceType)];
        adapterPart = adapterIndex < 0 ? 0xffffffffull : uint64_t(adapterIndex);
    }
    m_derivedSeed = mix64(
        m_options.seed ^ hashString(m_scenarioName) ^ (uint64_t(device->getDeviceType()) << 32) ^ adapterPart
    );
    m_rng.reset(m_derivedSeed);
    m_budget.reset(uint64_t(m_options.resourceBudgetMB) * 1024ull * 1024ull);
    m_initialResourceCount = rhi::testing::gResourceCount.load();
    m_start = std::chrono::steady_clock::now();
    m_lastReport = m_start;
}

bool StressContext::shouldContinue() const
{
    if (m_options.iterations != 0)
        return m_stats.iterations < m_options.iterations;
    return elapsedSeconds() < double(m_options.durationSec);
}

uint64_t StressContext::beginIteration()
{
    uint64_t iteration = m_stats.iterations++;
    return iteration;
}

bool StressContext::shouldValidate() const
{
    return m_options.validateEvery != 0 && m_stats.iterations != 0 &&
           (m_stats.iterations % m_options.validateEvery) == 0;
}

void StressContext::submit(ComPtr<ICommandBuffer> commandBuffer)
{
    REQUIRE(commandBuffer != nullptr);
    REQUIRE_CALL(m_queue->submit(commandBuffer));
    m_stats.submits++;
    m_submittedSinceWait++;

    uint32_t inflight = std::max(1u, m_options.inflight);
    if (m_submittedSinceWait >= inflight)
        wait();
}

void StressContext::wait()
{
    REQUIRE_CALL(m_queue->waitOnHost());
    m_stats.waits++;
    m_submittedSinceWait = 0;
}

void StressContext::finalWait()
{
    wait();
}

void StressContext::flushReleasedResources()
{
    auto encoder = m_queue->createCommandEncoder();
    REQUIRE(encoder != nullptr);
    submit(encoder->finish());
    wait();
}

void StressContext::recordOperation(std::string operation)
{
    std::ostringstream stream;
    stream << "#" << m_stats.iterations << " " << operation;
    m_log.add(stream.str());
}

void StressContext::reportProgressIfDue()
{
    if (m_options.reportIntervalSec == 0)
        return;

    auto now = std::chrono::steady_clock::now();
    double sinceLast = std::chrono::duration<double>(now - m_lastReport).count();
    if (sinceLast < double(m_options.reportIntervalSec))
        return;

    m_lastReport = now;
    if (rhi::testing::options().verbose)
        MESSAGE(summaryString());
}

void StressContext::reportFinal()
{
    MESSAGE(summaryString());
}

void StressContext::captureState() const
{
    MESSAGE(summaryString() << "\nRecent stress operations:\n" << m_log.toString());
}

uint64_t StressContext::currentResourceCount() const
{
    return rhi::testing::gResourceCount.load();
}

double StressContext::elapsedSeconds() const
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
}

std::string StressContext::summaryString() const
{
    std::ostringstream stream;
    stream << "stress scenario=" << m_scenarioName << " device=" << deviceTypeToString(m_device->getDeviceType())
           << " baseSeed=0x" << std::hex << m_options.seed << " derivedSeed=0x" << m_derivedSeed << std::dec
           << " elapsedSec=" << std::fixed << std::setprecision(2) << elapsedSeconds()
           << " iterations=" << m_stats.iterations << " submits=" << m_stats.submits << " waits=" << m_stats.waits
           << " validations=" << m_stats.validations << " budgetCurrentMB="
           << (m_budget.currentBytes() / (1024ull * 1024ull)) << " budgetPeakMB="
           << (m_budget.peakBytes() / (1024ull * 1024ull)) << " resourceDelta="
           << int64_t(currentResourceCount()) - int64_t(m_initialResourceCount);
    return stream.str();
}

} // namespace rhi::testing::stress

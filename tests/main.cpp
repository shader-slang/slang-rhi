#include "testing.h"
#include <slang-rhi/agility-sdk.h>

#include <cstdlib>
#include <limits>

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "doctest-reporter.h"

// Due to current issues in slang we don't enable Agility SDK yet
SLANG_RHI_EXPORT_AGILITY_SDK

namespace rhi::testing {

// Helpers to get current test suite and case name.
// See https://github.com/doctest/doctest/issues/345.
// Has to be defined in the same file as DOCTEST_CONFIG_IMPLEMENT
std::string getCurrentTestSuiteName()
{
    return doctest::detail::g_cs->currentTest->m_test_suite;
}
std::string getCurrentTestCaseName()
{
    return doctest::detail::g_cs->currentTest->m_name;
}

} // namespace rhi::testing

static bool parseUInt32Option(int argc, const char** argv, const char* pattern, uint32_t& outValue)
{
    doctest::String value;
    if (!doctest::parseOption(argc, argv, pattern, &value))
        return false;

    char* end = nullptr;
    unsigned long parsed = std::strtoul(value.c_str(), &end, 0);
    if (!end || *end != '\0' || parsed > std::numeric_limits<uint32_t>::max())
        return false;

    outValue = uint32_t(parsed);
    return true;
}

static bool parseUInt64Option(int argc, const char** argv, const char* pattern, uint64_t& outValue)
{
    doctest::String value;
    if (!doctest::parseOption(argc, argv, pattern, &value))
        return false;

    char* end = nullptr;
    unsigned long long parsed = std::strtoull(value.c_str(), &end, 0);
    if (!end || *end != '\0')
        return false;

    outValue = uint64_t(parsed);
    return true;
}

int main(int argc, const char** argv)
{
    // Store path to the executable.
    rhi::testing::exePath() = argv[0];

    rhi::testing::cleanupTestTempDirectories();

#if SLANG_RHI_DEBUG
    rhi::DebugLayerOptions debugLayerOptions{};
    debugLayerOptions.coreValidation = true;
    rhi::getRHI()->setDebugLayerOptions(debugLayerOptions);
#endif

    // Parse extra command line options.
    {
        auto& options = rhi::testing::options();
        if (doctest::parseFlag(argc, argv, "verbose"))
        {
            options.verbose = true;
        }
        if (doctest::parseFlag(argc, argv, "check-devices"))
        {
            options.checkDevices = true;
        }
        if (doctest::parseFlag(argc, argv, "list-devices"))
        {
            options.listDevices = true;
        }
        if (doctest::parseFlag(argc, argv, "stress"))
        {
            options.stress.enabled = true;
        }
        if (doctest::parseFlag(argc, argv, "stress-enable-rt"))
        {
            options.stress.enableRayTracing = true;
        }
        parseUInt32Option(argc, argv, "stress-duration-sec=", options.stress.durationSec);
        parseUInt32Option(argc, argv, "stress-iterations=", options.stress.iterations);
        parseUInt64Option(argc, argv, "stress-seed=", options.stress.seed);
        parseUInt32Option(argc, argv, "stress-inflight=", options.stress.inflight);
        parseUInt32Option(argc, argv, "stress-resource-budget-mb=", options.stress.resourceBudgetMB);
        parseUInt32Option(argc, argv, "stress-report-interval-sec=", options.stress.reportIntervalSec);
        parseUInt32Option(argc, argv, "stress-log-ops=", options.stress.logOps);
        parseUInt32Option(argc, argv, "stress-validate-every=", options.stress.validateEvery);
        parseUInt32Option(argc, argv, "stress-shader-corpus=", options.stress.shaderCorpus);
        std::vector<doctest::String> strings;
        if (doctest::parseCommaSepArgs(argc, argv, "select-devices=", strings))
        {
            options.deviceSelected.fill(false);
            for (const auto& str : strings)
            {
                if (str == "*")
                {
                    options.deviceSelected.fill(true);
                    continue;
                }
                for (rhi::DeviceType deviceType : rhi::testing::kPlatformDeviceTypes)
                {
                    doctest::String deviceTypeStr = rhi::testing::deviceTypeToString(deviceType);
                    if (str == deviceTypeStr || str.substr(0, deviceTypeStr.size()) == deviceTypeStr)
                    {
                        options.deviceSelected[size_t(deviceType)] = true;
                        if (str.size() > deviceTypeStr.size() + 1 && str[deviceTypeStr.size()] == ':')
                        {
                            int adapterIndex = atoi(str.c_str() + deviceTypeStr.size() + 1);
                            options.deviceAdapterIndex[size_t(deviceType)] = adapterIndex;
                        }
                        break;
                    }
                }
            }
        }
        doctest::parseIntOption(argc, argv, "optix-version=", doctest::option_int, options.optixVersion);
    }

    int result = 1;
    {
        doctest::Context context(argc, argv);

        context.setOption("--reporters", "custom");
        context.setOption("--order-by", "name");

        // Select specific test suite to run
        // context.setOption("-tc", "shader-cache-*");
        // Report successful tests
        // context.setOption("success", true);

        result = context.run();

        bool noSilentSkips = rhi::testing::checkNoSilentGpuSkips();
        if (result == 0 && !noSilentSkips)
            result = 1;

        rhi::testing::releaseCachedDevices();
    }

    rhi::testing::cleanupTestTempDirectories();

    rhi::destroyRHI();

#if SLANG_RHI_ENABLE_REF_OBJECT_TRACKING
    if (!rhi::RefObjectTracker::instance().objects.empty())
    {
        std::cerr << std::to_string(rhi::RefObjectTracker::instance().objects.size()) << " leaked objects detected!"
                  << std::endl;
        std::cerr << "Leaked objects detected!" << std::endl;
        for (auto obj : rhi::RefObjectTracker::instance().objects)
        {
            std::cerr << "Leaked object: " << obj << std::endl;
        }
        return 1;
    }
#endif

#if SLANG_RHI_DEBUG
    if (rhi::RefObject::getObjectCount() > 0)
    {
        std::cerr << std::to_string(rhi::RefObject::getObjectCount()) << " leaked objects detected!" << std::endl;
        return 1;
    }
#endif

    return result;
}

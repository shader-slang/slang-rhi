#include "testing.h"
#include "memory-report.h"
#include <slang-rhi/agility-sdk.h>

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "doctest-reporter.h"

#include <charconv>
#include <cstdio>
#include <string_view>

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

        doctest::String d3d12ShaderModel;
        if (doctest::parseOption(argc, argv, "d3d12-shader-model=", &d3d12ShaderModel))
        {
            std::string_view value = d3d12ShaderModel.c_str();
            size_t separator = value.find('.');
            unsigned int major = 0;
            unsigned int minor = 0;
            auto parseComponent = [](std::string_view component, unsigned int& result)
            {
                auto [end, error] = std::from_chars(component.data(), component.data() + component.size(), result);
                return !component.empty() && error == std::errc() && end == component.data() + component.size();
            };
            bool validFormat = separator != std::string_view::npos &&
                               parseComponent(value.substr(0, separator), major) &&
                               parseComponent(value.substr(separator + 1), minor);
            bool validVersion = (major == 5 && minor == 1) || (major == 6 && minor <= 10);
            if (!validFormat || !validVersion)
            {
                std::fprintf(
                    stderr,
                    "Invalid D3D12 shader model '%s'; expected 5.1 or 6.0 through 6.10 in major.minor format.\n",
                    d3d12ShaderModel.c_str()
                );
                return 1;
            }
            options.d3d12ShaderModel = (major << 4) | minor;
        }

        if (doctest::parseFlag(argc, argv, "d3d12-disable-nvapi"))
        {
            options.d3d12DisableNVAPI = true;
        }

        doctest::parseIntOption(argc, argv, "optix-version=", doctest::option_int, options.optixVersion);

        if (doctest::parseFlag(argc, argv, "memory-report"))
        {
            options.memoryReport = true;
            options.printMemoryReport = true;
        }

        doctest::String memoryReportFile;
        if (doctest::parseOption(argc, argv, "memory-report-file=", &memoryReportFile))
        {
            options.memoryReport = true;
            options.memoryReportFile = memoryReportFile.c_str();
        }
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
        rhi::testing::sampleMemoryReport("after-release-cached-devices");
    }

    rhi::testing::cleanupTestTempDirectories();

    rhi::destroyRHI();

    rhi::testing::sampleMemoryReport("after-destroy-rhi");
    rhi::testing::printMemoryReport();
    rhi::testing::writeMemoryReport();

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

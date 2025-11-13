#include <doctest.h>

#include <mutex>
#include <cstdarg>

#include "testing.h"

namespace doctest {

struct CustomReporter : public IReporter
{
    // caching pointers/references to objects of these types - safe to do
    std::ostream& stream;
    const ContextOptions& opt;
    ConsoleReporter consoleReporter;
    const TestCaseData* tc;

    int cursorPos = 0;
    int testCaseStartCursorPos = 0;
    Timer runTimer;

    const int resultPos = 64;

    // constructor has to accept the ContextOptions by ref as a single argument
    CustomReporter(const ContextOptions& in)
        : stream(*in.cout)
        , opt(in)
        , consoleReporter(in)
    {
    }

    void report_query(const QueryData& in) override
    {
        consoleReporter.report_query(in);
        if (opt.help)
        {
            // clang-format off
            stream << Color::Cyan << "\n[doctest] " << Color::None;
            stream << "Additional slang-rhi specific options. Available:\n\n";
            stream << " -verbose                              print messages from the slang-rhi layer\n";
            stream << " -check-devices                        print information about used GPU devices on startup\n";
            stream << " -list-devices                         print information about available GPU devices and exit\n";
            stream << " -select-devices=<device>[,...|*]      select which devices to use for testing (default: all)\n";
            stream << "                                       <device> can be d3d11, d3d12, vulkan, metal, cpu, cuda, wgpu\n";
            stream << "                                       to select a specific adapter, the adapter index can be appended after a colon (i.e. d3d12:1)\n";
            stream << "                                       use * to select all available devices\n";
            stream << " -optix-version=<version>              select a specific OptiX version to use, e.g. 80100 for version 8.1\n";
            // clang-format on
        }
    }

    void test_run_start() override
    {
        stream << Color::None;

        if (rhi::testing::options().listDevices)
        {
            printf("Available devices:\n");
            for (rhi::DeviceType deviceType : rhi::testing::kPlatformDeviceTypes)
            {
                for (uint32_t i = 0;; ++i)
                {
                    rhi::IAdapter* adapter = rhi::getRHI()->getAdapter(deviceType, i);
                    if (!adapter)
                    {
                        break;
                    }
                    char deviceID[16];
                    snprintf(deviceID, sizeof(deviceID), "%s:%u", rhi::testing::deviceTypeToString(deviceType), i);
                    const rhi::AdapterInfo& info = adapter->getInfo();
                    printf("- %-8s \"%s\"\n", deviceID, info.name);
                }
            }
            exit(0);
        }

        runTimer.start();
        consoleReporter.test_run_start();

        if (rhi::testing::options().checkDevices)
        {
            checkDevices();
        }
    }

    void test_run_end(const TestRunStats& in) override
    {
        stream << Color::None;

        fill(79, '-');
        printf("\n");
        double seconds = runTimer.getElapsedSeconds();
        printf("Total time: %.2fs\n", seconds);

        consoleReporter.test_run_end(in);
    }

    void test_case_start(const TestCaseData& in) override
    {
        consoleReporter.test_case_start(in);
        tc = &in;
        color(Color::None);
        printf("%s ", tc->m_name);
        testCaseStartCursorPos = cursorPos;
    }

    // called when a test case is reentered because of unfinished subcases
    void test_case_reenter(const TestCaseData& in) override { consoleReporter.test_case_reenter(in); }

    void test_case_end(const CurrentTestCaseStats& in) override
    {
        color(Color::None);
        if (cursorPos != testCaseStartCursorPos)
        {
            ensure_newline();
            printf("%s ", tc->m_name);
        }
        fill(resultPos);
        if (const char* msg = rhi::testing::getSkipMessage(tc))
        {
            color(Color::Yellow);
            print("SKIPPED");
            color(Color::LightGrey);
            printf(" (%s)\n", msg);
        }
        else if (in.failure_flags)
        {
            color(Color::Red);
            print("FAILED");
            color(Color::LightGrey);
            printf(" (%.2fs)\n", in.seconds);
        }
        else
        {
            color(Color::Green);
            printf("PASSED");
            color(Color::LightGrey);
            printf(" (%.2fs)\n", in.seconds);
        }
    }

    void test_case_exception(const TestCaseException& in) override
    {
        ensure_newline();
        consoleReporter.test_case_exception(in);
    }

    void subcase_start(const SubcaseSignature& in) override { consoleReporter.subcase_start(in); }

    void subcase_end() override { consoleReporter.subcase_end(); }

    void log_assert(const AssertData& in) override
    {
        // don't include successful asserts by default - this is done here
        // instead of in the framework itself because doctest doesn't know
        // if/when a reporter/listener cares about successful results
        if (!in.m_failed && !opt.success)
            return;

        ensure_newline();
        consoleReporter.log_assert(in);
    }

    void log_message(const MessageData& in) override
    {
        ensure_newline();
        consoleReporter.log_message(in);
    }

    void test_case_skipped(const TestCaseData& /*in*/) override {}

private:
    void color(Color::Enum c)
    {
        if (!opt.no_colors)
            stream << c;
    }

    void fill(int x, char c = ' ')
    {
        while (cursorPos < x)
        {
            stream << c;
            cursorPos++;
        }
    }

    void print(std::string_view str)
    {
        stream << str;
        stream.flush();
        for (char c : str)
            cursorPos = c == '\n' ? 0 : cursorPos + 1;
    }

    void printf(const char* fmt, ...)
    {
        std::va_list args;
        va_start(args, fmt);
        char buffer[1024];
        std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        print(buffer);
    }

    void println(std::string_view str)
    {
        print(str);
        print("\n");
    }

    void ensure_newline()
    {
        if (cursorPos != 0)
            print("\n");
    }

    void checkDevices()
    {
        printSeparator();
        printf("Checking for available devices:\n");
        for (rhi::DeviceType deviceType : rhi::testing::kPlatformDeviceTypes)
        {
            if (!rhi::testing::isDeviceTypeSelected(deviceType))
                continue;
            printSeparator();
            printf("%s: ", rhi::getRHI()->getDeviceTypeName(deviceType));
            rhi::testing::DeviceAvailabilityResult result = rhi::testing::checkDeviceTypeAvailable(deviceType);
            if (result.available)
            {
                color(Color::Green);
                printf("supported\n");
                color(Color::None);
                printDeviceInfo(result.device);
            }
            else
            {
                color(Color::Yellow);
                printf("not supported (%s)\n", result.error.c_str());
                color(Color::None);
            }
            if (result.debugCallbackOutput.size() > 0)
                printf("Debug callback output: %s\n", result.debugCallbackOutput.c_str());
            if (result.diagnostics.size() > 0)
                printf("Slang diagnostics: %s\n", result.diagnostics.c_str());
        }
        printSeparator();
    }

    void printSeparator()
    {
        color(Color::Grey);
        fill(79, '-');
        printf("\n");
        color(Color::None);
    }

    void printDeviceInfo(rhi::IDevice* device)
    {
        const rhi::DeviceInfo& deviceInfo = device->getInfo();
        printf("Adapter Name: %s\n", deviceInfo.adapterName);
        printf("Adapter LUID: ");
        for (size_t i = 0; i < sizeof(rhi::AdapterLUID); i++)
            printf("%02x", deviceInfo.adapterLUID.luid[i]);
        printf("\n");
        {
            uint32_t featureCount;
            device->getFeatures(&featureCount, nullptr);
            std::vector<rhi::Feature> features(featureCount);
            device->getFeatures(&featureCount, features.data());
            printf("Features:\n");
            for (uint32_t i = 0; i < featureCount; i++)
                printf("%s ", rhi::getRHI()->getFeatureName(features[i]));
            printf("\n");
        }
        {
            uint32_t capabilityCount;
            device->getCapabilities(&capabilityCount, nullptr);
            std::vector<rhi::Capability> capabilities(capabilityCount);
            device->getCapabilities(&capabilityCount, capabilities.data());
            printf("Capabilities:\n");
            for (uint32_t i = 0; i < capabilityCount; i++)
                printf("%s ", rhi::getRHI()->getCapabilityName(capabilities[i]));
            printf("\n");
        }
        if (device->hasFeature(rhi::Feature::CooperativeVector))
        {
            uint32_t propertiesCount;
            device->getCooperativeVectorProperties(nullptr, &propertiesCount);
            std::vector<rhi::CooperativeVectorProperties> properties(propertiesCount);
            device->getCooperativeVectorProperties(properties.data(), &propertiesCount);
            printf("Cooperative Vector Properties:\n");
            printf("inputType inputInterpretation matrixInterpretation biasInterpretation resultType transpose\n");
            for (const auto& prop : properties)
            {
                printf(
                    "%-9s %-19s %-20s %-18s %-10s %-10s\n",
                    rhi::enumToString(prop.inputType),
                    rhi::enumToString(prop.inputInterpretation),
                    rhi::enumToString(prop.matrixInterpretation),
                    rhi::enumToString(prop.biasInterpretation),
                    rhi::enumToString(prop.resultType),
                    prop.transpose ? "true" : "false"
                );
            }
        }
    }
};

// "1" is the priority - used for ordering when multiple reporters are used
REGISTER_REPORTER("custom", 1, CustomReporter);

} // namespace doctest

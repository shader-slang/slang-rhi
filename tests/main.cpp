#include "testing.h"
#include <slang-rhi/agility-sdk.h>

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

int main(int argc, char** argv)
{
    rhi::testing::cleanupTestTempDirectories();

#if SLANG_RHI_DEBUG
    rhi::getRHI()->enableDebugLayers();
#endif

    // Pass extra command line arguments to the custom reporter.
    for (int i = 1; i < argc; ++i)
    {
        auto& options = rhi::testing::options();
        if (strcmp(argv[i], "-verbose") == 0)
        {
            options.verbose = true;
        }
        if (strcmp(argv[i], "-check-devices") == 0)
        {
            options.checkDevices = true;
        }
    }

    int result = 1;
    {
        doctest::Context context(argc, argv);

        context.setOption("--reporters", "custom");

        // Select specific test suite to run
        // context.setOption("-tc", "shader-cache-*");
        // Report successful tests
        // context.setOption("success", true);

        result = context.run();

        rhi::testing::releaseCachedDevices();
    }

    rhi::testing::cleanupTestTempDirectories();

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

// SPDX-License-Identifier: Apache-2.0

#include "testing.h"

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

// TODO_TESTING
// SLANG_RHI_EXPORT_AGILITY_SDK

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

    int result = 1;
    {
        doctest::Context context(argc, argv);

        // Select specific test suite to run
        // context.setOption("-tc", "shader-cache-*");
        // Report successful tests
        // context.setOption("success", true);

        result = context.run();
    }

    rhi::testing::cleanupTestTempDirectories();

    return result;
}

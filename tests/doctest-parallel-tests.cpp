// Standalone integration fixture: deliberately depends only on doctest and the extension.
#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_NO_UNPREFIXED_OPTIONS
#define DOCTEST_CONFIG_OPTIONS_PREFIX "fixture-"
#define DOCTEST_CONFIG_IMPLEMENT
#define DOCTEST_PARALLEL_IMPLEMENT
#include "doctest-parallel.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
bool skipCurrent = false;
const char metadataBytes[] = "a line\nwith a zero\0and quotes\"";
const std::string metadata(metadataBytes, sizeof(metadataBytes) - 1);
} // namespace

DOCTEST_TEST_CASE("fixture-pass-a")
{
    DOCTEST_CHECK(1 + 1 == 2);
}

DOCTEST_TEST_CASE("fixture-subcases")
{
    DOCTEST_SUBCASE("first")
    {
        DOCTEST_CHECK(true);
    }
    DOCTEST_SUBCASE("second")
    {
        DOCTEST_CHECK(true);
    }
}

DOCTEST_TEST_CASE("fixture-runtime-skip")
{
    skipCurrent = true;
}

DOCTEST_TEST_CASE("fixture-failure" * doctest::skip())
{
    DOCTEST_CHECK(false);
}

DOCTEST_TEST_CASE("fixture-crash" * doctest::skip())
{
    std::_Exit(17); // Abrupt exit without CRT dialogs or worker cleanup.
}

DOCTEST_TEST_CASE("fixture-hang" * doctest::skip())
{
    std::this_thread::sleep_for(std::chrono::seconds(60));
}

int main(int argc, char** argv)
{
    bool badMetadata = false;
    bool failStart = false, failStop = false, configureFilter = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string argument = argv[i];
        failStart = failStart || argument == "--fixture-start-fails";
        failStop = failStop || argument == "--fixture-stop-fails";
        configureFilter = configureFilter || argument == "--fixture-filter";
    }
    doctest::parallel::Options options;
    options.workerStart = [&](const std::string&)
    {
        std::cout << "fixture worker start\n";
        return failStart ? 1 : 0;
    };
    options.workerStop = [&]()
    {
        std::cout << "fixture worker stop\n";
        return failStop ? 1 : 0;
    };
    options.configureContext = [&](doctest::Context& context)
    {
        skipCurrent = false;
        context.setOption(DOCTEST_CONFIG_OPTIONS_PREFIX "no-intro", true);
        if (configureFilter)
            context.addFilter(DOCTEST_CONFIG_OPTIONS_PREFIX "test-case-exclude", "fixture-pass-b");
    };
    options.isSkipped = [](const doctest::TestCaseData&)
    {
        return skipCurrent;
    };
    options.collectData = []()
    {
        return metadata;
    };
    options.onResult = [&](const doctest::parallel::Result& result)
    {
        if (!result.workerFailed && result.data != metadata)
            badMetadata = true;
        std::cout << "fixture result " << result.name << ' ' << result.executed << '\n';
    };
    options.coordinatorStop = [&]()
    {
        return badMetadata ? 1 : 0;
    };
    // Embedding must preserve a context installed by the caller.
    doctest::Context outer;
    outer.setAsDefaultForAssertsOutOfTestCases();
    const auto* prior = doctest::getContextOptions();
    if (auto result = doctest::parallel::run(argc, argv, options))
    {
        if (doctest::getContextOptions() != prior)
            return 12;
        return *result;
    }
    if (doctest::getContextOptions() != prior)
        return 12;
    doctest::Context context(argc, argv);
    return context.run();
}

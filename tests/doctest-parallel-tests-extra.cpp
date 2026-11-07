// A second translation unit verifies declaration-only inclusion and prefixed macros.
#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_NO_UNPREFIXED_OPTIONS
#define DOCTEST_CONFIG_OPTIONS_PREFIX "fixture-"
#include "doctest-parallel.h"

DOCTEST_TEST_CASE("fixture-pass-b")
{
    DOCTEST_CHECK(true);
}

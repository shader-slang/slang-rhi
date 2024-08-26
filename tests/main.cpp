// SPDX-License-Identifier: Apache-2.0

#include "testing.h"

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

// TODO_GFX
// SLANG_RHI_EXPORT_AGILITY_SDK

int main(int argc, char** argv)
{
    int result = 1;
    {
        doctest::Context context(argc, argv);

        // Select specific test suite to run
        // context.setOption("-ts", "formats");
        // Report successful tests
        // context.setOption("success", true);

        result = context.run();
    }

    return result;
}

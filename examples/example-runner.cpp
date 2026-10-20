// Example runner: launch a registered example across the selected backends.
// Use --example to choose the initial example or --list-examples to list them.
// F5 opens the example picker; the overlay lists shared controls and F1 toggles it.

#include "base/example.h"
#include <slang-rhi/agility-sdk.h>

SLANG_RHI_EXPORT_AGILITY_SDK

int main(int argc, const char** argv)
{
    return rhi::detail::runRegisteredExamples(argc, argv, "triangle");
}

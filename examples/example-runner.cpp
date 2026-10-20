#include "base/example.h"
#include <slang-rhi/agility-sdk.h>

SLANG_RHI_EXPORT_AGILITY_SDK

int main(int argc, const char** argv)
{
    return rhi::detail::runRegisteredExamples(argc, argv, "triangle");
}

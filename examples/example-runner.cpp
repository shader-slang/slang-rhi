#include "base/example.h"

int main(int argc, const char** argv)
{
    return rhi::detail::runRegisteredExamples(argc, argv, "triangle");
}

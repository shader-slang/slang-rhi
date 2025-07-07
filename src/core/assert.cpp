#include "assert.h"

#include <cstdlib>
#include <cstdio>

namespace rhi {

thread_local int gDisableAssert;

ScopedDisableAssert::ScopedDisableAssert()
{
    gDisableAssert++;
}

ScopedDisableAssert::~ScopedDisableAssert()
{
    gDisableAssert--;
}


void handleAssert(const char* message, const char* file, int line)
{
    if (gDisableAssert == 0)
    {
        std::fprintf(stderr, "Assertion failed: %s\n", message);
        std::fprintf(stderr, "At %s:%d\n", file, line);
        std::abort();
    }
}

} // namespace rhi
